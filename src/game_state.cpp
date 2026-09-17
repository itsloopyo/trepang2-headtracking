// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "game_state.h"

#include <atomic>
#include <cmath>

#include "logging.h"
#include "ue_call.h"
#include "ue_reflect.h"
#include "ue_vm.h"

#include "cameraunlock/unreal/ue_runtime.h"

namespace t2_ht::game_state {

namespace {

namespace ue = ::cameraunlock::unreal;

// The drawn view and the first-person camera agree to within the engine's own
// camera shake and recoil in gameplay; a cutscene or a death camera is metres
// away or pointed elsewhere.
constexpr double kMaxViewOffsetCm = 60.0;
constexpr double kMaxViewAngleDeg = 25.0;

constexpr double kRadiansToDegrees = 57.29577951308232;

ue_vm::ResolveRetry g_retry;
bool g_resolved = false;

std::uintptr_t g_staticsCdo = 0;
ue_call::Function g_isGamePaused;       // GameplayStatics::IsGamePaused
ue_call::Function g_isLoading;          // CPPFPSGameMode::IsLoading
ue_call::Function g_isPlayingCutscene;  // BaseGameMode_C::GetIsPlayingCutscene
std::size_t g_worldGameModeOffset = 0;

struct ControllerFields {
    std::uintptr_t Class = 0;
    ue_reflect::FieldInfo Cursor;
    ue_reflect::FieldInfo PhotoMode;
    bool HavePhotoMode = false;
};
ControllerFields g_controllerFields;

bool Resolve(std::uintptr_t controller) {
    if (g_resolved) return true;
    if (!g_retry.Due() || !ue_vm::Ready()) return false;

    g_staticsCdo = ue_call::DefaultObject("GameplayStatics");
    const std::uintptr_t world = ue::OuterObject(ue::OuterObject(controller));
    ue_reflect::FieldInfo gameMode;
    if (!g_staticsCdo || !world ||
        !ue_reflect::FindPropertyInChain(ue_call::ClassOf(world), "AuthorityGameMode", gameMode) ||
        gameMode.Size != sizeof(std::uintptr_t))
        return false;

    // GetIsPlayingCutscene is a blueprint function, so its answer comes back in
    // an output parameter named by the blueprint rather than in ReturnValue.
    if (!g_isGamePaused.Resolve("GameplayStatics", "IsGamePaused",
                                {{"WorldContextObject", sizeof(std::uintptr_t)}, {"ReturnValue", 1}}) ||
        !g_isLoading.Resolve("CPPFPSGameMode", "IsLoading", {{"ReturnValue", 1}}) ||
        !g_isPlayingCutscene.Resolve("BaseGameMode_C", "GetIsPlayingCutscene", {{"PlayingCutscene", 1}}))
        return false;

    g_worldGameModeOffset = gameMode.Offset;
    g_resolved = true;
    Log::Line("gate: resolved (IsGamePaused, IsLoading, GetIsPlayingCutscene; World.AuthorityGameMode=+0x%zx)",
              g_worldGameModeOffset);
    return true;
}

void RefreshControllerFields(std::uintptr_t controller) {
    const std::uintptr_t cls = ue_call::ClassOf(controller);
    if (!cls || cls == g_controllerFields.Class) return;
    g_controllerFields = ControllerFields{};
    g_controllerFields.Class = cls;
    ue_reflect::FindPropertyInChain(cls, "bShowMouseCursor", g_controllerFields.Cursor);
    // Declared on the game's PlayerControllerBP; the photo mode ("Kamera").
    g_controllerFields.HavePhotoMode =
        ue_reflect::FindPropertyInChain(cls, "KameraMode", g_controllerFields.PhotoMode) &&
        g_controllerFields.PhotoMode.BoolMask != 0;
    Log::Line("gate: controller class %s (cursor flag %s, photo-mode flag %s)",
              ue::ObjectName(cls).c_str(), g_controllerFields.Cursor.BoolMask ? "found" : "MISSING",
              g_controllerFields.HavePhotoMode ? "found" : "absent");
}

// False when the flag could not be read, which the caller treats as blocking.
bool AskFlag(const ue_call::Function& fn, std::uintptr_t self, std::size_t index, bool& out,
             std::uintptr_t context = 0) {
    if (!self) return false;
    ue_call::Frame frame(fn);
    if (context) frame.Set(0, context);
    if (!frame.Call(self)) return false;
    out = (frame.Get<std::uint8_t>(index) & 1u) != 0;
    return true;
}

// The authority game mode, when it is one of the game's own (the front end and
// every level run a BaseGameMode_C subclass). Cached per object, because the
// class walk is a string compare per link and this runs every frame.
std::uintptr_t GameMode(std::uintptr_t controller) {
    const std::uintptr_t world = ue::OuterObject(ue::OuterObject(controller));
    std::uintptr_t gameMode = 0;
    if (!world || !ue::SafeReadPtr(world + g_worldGameModeOffset, gameMode) || !gameMode) return 0;
    static std::uintptr_t s_checked = 0;
    static bool s_ours = false;
    if (gameMode != s_checked) {
        s_checked = gameMode;
        s_ours = ue_call::IsA(gameMode, "BaseGameMode_C");
        if (!s_ours)
            Log::Line("gate: game mode %s is not a BaseGameMode_C - treated as not gameplay",
                      ue::ClassName(gameMode).c_str());
    }
    return s_ours ? gameMode : 0;
}

}  // namespace

Verdict Evaluate(std::uintptr_t controller, const player_rig::Snapshot& rig,
                 const ue::FVector& cleanLocation, const ue::FRotator& cleanRotation) {
    Verdict v;
    if (!rig.Pawn || !rig.FirstPersonCamera.Valid || !Resolve(controller)) {
        v.Why = Blocker::NoPlayer;
        return v;
    }
    if (rig.Dead) {
        v.Why = Blocker::Dead;
        return v;
    }

    const std::uintptr_t gameMode = GameMode(controller);
    bool loading = true;
    if (!AskFlag(g_isLoading, gameMode, 0, loading) || loading) {
        v.Why = Blocker::Loading;
        return v;
    }

    RefreshControllerFields(controller);
    bool cursor = true;
    if (!ue_reflect::ReadBool(controller, g_controllerFields.Cursor, cursor) || cursor) {
        v.Why = Blocker::Cursor;
        return v;
    }
    bool paused = true;
    if (!AskFlag(g_isGamePaused, g_staticsCdo, 1, paused, controller) || paused) {
        v.Why = Blocker::Paused;
        return v;
    }
    bool cutscene = true;
    if (!AskFlag(g_isPlayingCutscene, gameMode, 0, cutscene) || cutscene) {
        v.Why = Blocker::Cutscene;
        return v;
    }
    bool photo = false;
    if (g_controllerFields.HavePhotoMode &&
        (!ue_reflect::ReadBool(controller, g_controllerFields.PhotoMode, photo) || photo)) {
        v.Why = Blocker::PhotoMode;
        return v;
    }

    const ue::FVector& cam = rig.FirstPersonCamera.Position;
    const double dx = cleanLocation.X - cam.X, dy = cleanLocation.Y - cam.Y, dz = cleanLocation.Z - cam.Z;
    v.ViewOffsetCm = std::sqrt(dx * dx + dy * dy + dz * dz);
    const ue::FVector viewFwd = ue::QuatRotateVec(
        ue::QuatFromEulerDeg(cleanRotation.Pitch, cleanRotation.Yaw, cleanRotation.Roll),
        ue::FVector{1.0, 0.0, 0.0});
    const ue::FVector& camFwd = rig.FirstPersonCamera.Forward;
    double dot = viewFwd.X * camFwd.X + viewFwd.Y * camFwd.Y + viewFwd.Z * camFwd.Z;
    dot = dot > 1.0 ? 1.0 : (dot < -1.0 ? -1.0 : dot);
    v.ViewAngleDeg = std::acos(dot) * kRadiansToDegrees;
    if (!(v.ViewOffsetCm <= kMaxViewOffsetCm) || !(v.ViewAngleDeg <= kMaxViewAngleDeg)) {
        v.Why = Blocker::NotFirstPerson;
        return v;
    }

    v.Why = Blocker::None;
    v.InGameplay = true;
    return v;
}

const char* BlockerName(Blocker b) {
    switch (b) {
        case Blocker::None:           return "gameplay";
        case Blocker::NoPlayer:       return "no player character (menu or loading)";
        case Blocker::Dead:           return "player dead";
        case Blocker::Loading:        return "level loading";
        case Blocker::Cursor:         return "mouse cursor up (menu)";
        case Blocker::Paused:         return "game paused";
        case Blocker::Cutscene:       return "cutscene";
        case Blocker::PhotoMode:      return "Kamera camera mode";
        case Blocker::NotFirstPerson: return "view is not the first-person camera";
    }
    return "unknown";
}

void LogTransitions(const Verdict& v) {
    static std::atomic<int> s_lastWhy{-1};
    const int why = static_cast<int>(v.Why);
    if (s_lastWhy.exchange(why) != why)
        Log::Line("gate: %s (view offset %.1fcm %.1fdeg)", BlockerName(v.Why), v.ViewOffsetCm,
                  v.ViewAngleDeg);
}

}  // namespace t2_ht::game_state
