// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "camera_fov.h"

#include <atomic>

#include "builds/build_registry.h"
#include "logging.h"
#include "ue_call.h"
#include "ue_reflect.h"
#include "ue_vm.h"

#include "cameraunlock/memory/safe_memory.h"
#include "cameraunlock/unreal/ue_runtime.h"

// ULocalPlayer::GetViewPoint, the render caller, fills its FMinimalViewInfo as
//
//     OutViewInfo      = CameraManager->GetCameraCacheView();
//     OutViewInfo.FOV  = CameraManager->GetFOVAngle();
//     PC->GetPlayerViewPoint(&OutViewInfo.Location, &OutViewInfo.Rotation);
//
// so the FOV this frame is drawn with is already in the struct when the hook
// runs, a fixed distance past the Location pointer it is handed.
namespace t2_ht::camera_fov {

namespace {

namespace ue = ::cameraunlock::unreal;

std::atomic<float> g_referenceAspect{kReferenceAspect};
std::atomic<int>   g_constraint{kConstraintUnknown};

std::uintptr_t g_playerClass = 0;
ue_reflect::FieldInfo g_playerField;
std::uintptr_t g_localPlayerClass = 0;
ue_reflect::FieldInfo g_constraintField;

ue_vm::ResolveRetry g_settingsRetry;
std::uintptr_t g_gameInstance = 0;
std::size_t g_saveObjOffset = 0;
std::size_t g_fovOffset = 0;

}  // namespace

float ReadRenderFov(const void* outLocation, const void* outRotation) {
    const auto& mvi = Offsets().MinimalViewInfoLayout;
    const auto locAddr = reinterpret_cast<std::uintptr_t>(outLocation);
    const auto rotAddr = reinterpret_cast<std::uintptr_t>(outRotation);
    float fov = 0.0f;
    if (rotAddr - locAddr != mvi.kRotationStride ||
        !ue::SafeReadFloat(locAddr + mvi.kFovOffset, fov) || !Plausible(fov))
        return 0.0f;
    float reference = 0.0f;
    if (ue::SafeReadFloat(locAddr + mvi.kAspectRatioOffset, reference) &&
        reference >= kMinReferenceAspect && reference <= kMaxReferenceAspect)
        g_referenceAspect.store(reference, std::memory_order_relaxed);
    return fov;
}

void RefreshAspectConstraint(std::uintptr_t controller) {
    const std::uintptr_t controllerClass = ue_call::ClassOf(controller);
    if (!controllerClass) return;
    if (controllerClass != g_playerClass) {
        g_playerClass = controllerClass;
        if (!ue_reflect::FindPropertyInChain(controllerClass, "Player", g_playerField) ||
            g_playerField.Size != sizeof(std::uintptr_t))
            g_playerField = ue_reflect::FieldInfo{};
    }
    if (g_playerField.Size != sizeof(std::uintptr_t)) return;

    std::uintptr_t localPlayer = 0;
    if (!ue::SafeReadPtr(controller + g_playerField.Offset, localPlayer) || !localPlayer) return;
    const std::uintptr_t playerClass = ue_call::ClassOf(localPlayer);
    if (playerClass != g_localPlayerClass) {
        g_localPlayerClass = playerClass;
        if (!ue_reflect::FindPropertyInChain(playerClass, "AspectRatioAxisConstraint", g_constraintField) ||
            g_constraintField.Size != 1)
            g_constraintField = ue_reflect::FieldInfo{};
    }
    if (g_constraintField.Size != 1) return;

    // One byte, because the field is one byte. A uint16 read of a field on the
    // last byte of a committed page faults, and the constraint then reads as
    // unresolved - which stands the mark down on an ultrawide display.
    std::uint8_t raw = 0;
    if (!cameraunlock::memory::SafeReadU8(localPlayer + g_constraintField.Offset, raw)) return;
    const int value = static_cast<int>(raw);
    const int next = (value == kMaintainYFOV || value == kMaintainXFOV || value == kMajorAxisFOV)
                         ? value : kConstraintUnknown;
    const int previous = g_constraint.exchange(next, std::memory_order_relaxed);
    if (previous != next)
        Log::Line("fov: AspectRatioAxisConstraint=%d (%s)", next,
                  next == kMaintainYFOV ? "MaintainYFOV" : next == kMaintainXFOV ? "MaintainXFOV"
                  : next == kMajorAxisFOV ? "MajorAxisFOV" : "unreadable");
}

// GameInstanceBP_C.SettingsSaveObj -> VideoSettings -> FOV_<guid>. Blueprint
// struct members carry a GUID suffix, so the member is found by its prefix.
// The save object is re-read every call: applying settings can replace it.
float BaseFov() {
    if (!g_gameInstance) {
        if (!g_settingsRetry.Due()) return 0.0f;
        std::uintptr_t found = 0;
        ue::ForEachUObject([&](std::uintptr_t obj) {
            if (ue::ClassName(obj) != "GameInstanceBP_C") return false;
            if (ue::ObjectName(obj).rfind("Default__", 0) == 0) return false;
            found = obj;
            return true;
        });
        if (!found) return 0.0f;
        ue_reflect::FieldInfo save, video, fov;
        std::uintptr_t saveObj = 0;
        if (!ue_reflect::FindPropertyInChain(ue_call::ClassOf(found), "SettingsSaveObj", save) ||
            save.Size != sizeof(std::uintptr_t) || !ue::SafeReadPtr(found + save.Offset, saveObj) || !saveObj ||
            !ue_reflect::FindPropertyInChain(ue_call::ClassOf(saveObj), "VideoSettings", video) ||
            !ue_reflect::FindPropertyByPrefix(ue_reflect::StructOf(video), "FOV_", fov) ||
            fov.TypeName != "FloatProperty")
            return 0.0f;
        g_gameInstance = found;
        g_saveObjOffset = save.Offset;
        g_fovOffset = video.Offset + fov.Offset;
        Log::Line("fov: game setting VideoSettings.%s at SettingsSaveObj+0x%zx", fov.Name.c_str(), g_fovOffset);
    }
    // Only a pointer that no longer reads drops the resolution. Re-resolving
    // walks the whole object table, on the render caller, so a save object not
    // built yet or a value that is not an angle has to be this frame's answer
    // rather than a reason to go looking again: both conditions last as long as
    // whatever caused them, and would re-scan on every retry tick throughout.
    std::uintptr_t saveObj = 0;
    if (!ue::SafeReadPtr(g_gameInstance + g_saveObjOffset, saveObj)) {
        g_gameInstance = 0;
        return 0.0f;
    }
    float fov = 0.0f;
    if (!saveObj || !ue::SafeReadFloat(saveObj + g_fovOffset, fov) || !Plausible(fov)) return 0.0f;
    return fov;
}

int AspectConstraint() { return g_constraint.load(std::memory_order_relaxed); }
float ReferenceAspect() { return g_referenceAspect.load(std::memory_order_relaxed); }

}  // namespace t2_ht::camera_fov
