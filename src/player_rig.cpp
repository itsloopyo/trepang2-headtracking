// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "player_rig.h"

#include <cmath>
#include <cstring>
#include <vector>

#include "logging.h"
#include "ue4_types.h"
#include "ue_call.h"
#include "ue_reflect.h"
#include "ue_vm.h"

#include "cameraunlock/unreal/ue_runtime.h"

namespace t2_ht::player_rig {

namespace {

namespace ue = ::cameraunlock::unreal;

ue_vm::ResolveRetry g_retry;
bool g_resolved = false;

ue_call::Function g_componentToWorld;    // SceneComponent::K2_GetComponentToWorld
std::size_t g_transformRotation = 0;
std::size_t g_transformTranslation = 0;
std::size_t g_pawnOffset = 0;

// The BasePlayer fields, resolved against the class they were first seen on; a
// different pawn class re-resolves.
struct PlayerFields {
    std::uintptr_t Class = 0;
    bool IsPlayer = false;
    ue_reflect::FieldInfo Camera;       // FirstPersonCameraComponent
    ue_reflect::FieldInfo Weapon;       // CurrentWeapon
    ue_reflect::FieldInfo Dead;         // bIsDead
    ue_reflect::FieldInfo WantZoom;     // bWantZoom
    ue_reflect::FieldInfo ZoomAlpha;    // ZoomAlpha
};
PlayerFields g_player;

bool Resolve(std::uintptr_t controller) {
    if (g_resolved) return true;
    if (!g_retry.Due() || !ue_vm::Ready()) return false;

    const std::uintptr_t controllerClass = ue_call::ClassOf(controller);
    if (!controllerClass) return false;
    ue_reflect::FieldInfo pawn;
    if (!ue_reflect::FindPropertyInChain(controllerClass, "Pawn", pawn) ||
        pawn.Size != sizeof(std::uintptr_t)) {
        Log::Line("player-rig: AController::Pawn is not in %s's property chain",
                  ue::ObjectName(controllerClass).c_str());
        return false;
    }
    if (!ue_reflect::VerifyBoolLayout(controllerClass)) return false;

    const std::uintptr_t transform = ue::FindLiveObject("ScriptStruct", "Transform", nullptr);
    std::vector<ue_reflect::FieldInfo> t;
    if (!transform ||
        !ue_reflect::ResolveAll("Transform", transform, {"Rotation", "Translation"}, t) ||
        t[0].Size != sizeof(ue4::FQuat) || t[1].Size != sizeof(ue4::FVector)) {
        Log::Line("player-rig: FTransform's layout did not resolve");
        return false;
    }
    if (!g_componentToWorld.Resolve("SceneComponent", "K2_GetComponentToWorld",
                                    {{"ReturnValue", ue_reflect::StructSize(transform)}}))
        return false;

    g_pawnOffset = pawn.Offset;
    g_transformRotation = t[0].Offset;
    g_transformTranslation = t[1].Offset;
    g_resolved = true;
    Log::Line("player-rig: resolved (Pawn=+0x%zx, FTransform Rotation=+0x%zx Translation=+0x%zx)",
              g_pawnOffset, g_transformRotation, g_transformTranslation);
    return true;
}

void RefreshPlayerFields(std::uintptr_t pawn) {
    const std::uintptr_t cls = ue_call::ClassOf(pawn);
    if (!cls || cls == g_player.Class) return;
    g_player = PlayerFields{};
    g_player.Class = cls;
    g_player.IsPlayer =
        ue_reflect::FindPropertyInChain(cls, "FirstPersonCameraComponent", g_player.Camera) &&
        g_player.Camera.Size == sizeof(std::uintptr_t) &&
        ue_reflect::FindPropertyInChain(cls, "CurrentWeapon", g_player.Weapon) &&
        g_player.Weapon.Size == sizeof(std::uintptr_t) &&
        ue_reflect::FindPropertyInChain(cls, "bIsDead", g_player.Dead) && g_player.Dead.BoolMask &&
        ue_reflect::FindPropertyInChain(cls, "bWantZoom", g_player.WantZoom) && g_player.WantZoom.BoolMask &&
        ue_reflect::FindPropertyInChain(cls, "ZoomAlpha", g_player.ZoomAlpha) &&
        g_player.ZoomAlpha.TypeName == "FloatProperty";
    Log::Line("player-rig: pawn class %s is %s", ue::ObjectName(cls).c_str(),
              g_player.IsPlayer ? "the player character" : "not a BasePlayer");
}

Transform ComponentTransform(std::uintptr_t component) {
    Transform out;
    if (!component) return out;
    ue_call::Frame frame(g_componentToWorld);
    if (!frame.Call(component)) return out;
    const unsigned char* ret = frame.At(0);
    ue4::FQuat q{};
    ue4::FVector p{};
    std::memcpy(&q, ret + g_transformRotation, sizeof(q));
    std::memcpy(&p, ret + g_transformTranslation, sizeof(p));
    out.Position = ue4::ToCore(p);
    out.Rotation = ue4::ToCore(q);
    out.Forward = ue::QuatRotateVec(out.Rotation, ue::FVector{1.0, 0.0, 0.0});
    const double len2 = out.Forward.X * out.Forward.X + out.Forward.Y * out.Forward.Y +
                        out.Forward.Z * out.Forward.Z;
    out.Valid = std::isfinite(out.Position.X) && std::isfinite(out.Position.Y) &&
                std::isfinite(out.Position.Z) && len2 > 0.98 && len2 < 1.02;
    return out;
}

std::uintptr_t ReadPointer(std::uintptr_t obj, const ue_reflect::FieldInfo& f) {
    std::uintptr_t v = 0;
    if (!ue::SafeReadPtr(obj + f.Offset, v)) return 0;
    return v;
}

}  // namespace

Snapshot Read(std::uintptr_t controller) {
    Snapshot s;
    if (!Resolve(controller)) return s;

    std::uintptr_t pawn = 0;
    if (!ue::SafeReadPtr(controller + g_pawnOffset, pawn) || !pawn) return s;
    RefreshPlayerFields(pawn);
    if (!g_player.IsPlayer) return s;

    s.Pawn = pawn;
    s.Camera = ReadPointer(pawn, g_player.Camera);
    s.FirstPersonCamera = ComponentTransform(s.Camera);
    s.Weapon = ReadPointer(pawn, g_player.Weapon);

    // An unreadable flag reads as the blocking answer for death and as "not
    // aiming" for the sights, which is the direction that leaves the game stock.
    bool dead = true;
    s.Dead = !ue_reflect::ReadBool(pawn, g_player.Dead, dead) || dead;
    bool wantZoom = false;
    ue_reflect::ReadBool(pawn, g_player.WantZoom, wantZoom);
    float alpha = 0.0f;
    if (!ue::SafeReadFloat(pawn + g_player.ZoomAlpha.Offset, alpha) || !std::isfinite(alpha)) alpha = 0.0f;
    s.AimingDownSights = wantZoom || alpha > 0.001f;
    return s;
}

}  // namespace t2_ht::player_rig
