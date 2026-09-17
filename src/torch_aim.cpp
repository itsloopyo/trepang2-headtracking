// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "torch_aim.h"

#include <cmath>
#include <cstdint>

#include "logging.h"
#include "ue4_types.h"
#include "ue_call.h"
#include "ue_reflect.h"
#include "ue_vm.h"

#include "cameraunlock/effects/head_follow_light.h"
#include "cameraunlock/unreal/ue_runtime.h"

namespace t2_ht::torch_aim {

namespace {

namespace ue = ::cameraunlock::unreal;
namespace effects = ::cameraunlock::effects;

bool  g_followsHead = false;
float g_multiplier = effects::kDefaultLightMultiplier;

ue_vm::ResolveRetry g_retry;
ue_call::Function g_setRelativeRotation;
bool g_haveSetter = false;

// The torch resolved against the pawn it hangs off. A respawn or a level load
// builds a new pawn and a new component, so the pointer is re-read whenever the
// pawn changes rather than cached for the session.
std::uintptr_t g_pawn = 0;
std::uintptr_t g_torch = 0;

// Whether the beam currently carries a rotation of ours, and which one. The
// game never writes this field itself - it ships the component at identity and
// leaves it there - so a turned beam stays turned until this module puts it
// back, and the comparison keeps a still head from dispatching a call a frame.
bool g_turned = false;
ue::FRotator g_written{0.0, 0.0, 0.0};

const char* g_state = "off";

// Below this the write would not change the beam by anything a player could
// see, and a torch on a still head would still cost one script dispatch per
// rendered frame.
constexpr double kMinChangeDeg = 0.01;

bool Differs(const ue::FRotator& a, const ue::FRotator& b) {
    return std::fabs(a.Pitch - b.Pitch) > kMinChangeDeg ||
           std::fabs(a.Yaw - b.Yaw) > kMinChangeDeg ||
           std::fabs(a.Roll - b.Roll) > kMinChangeDeg;
}

// Resolve the torch for the pawn in `rig`, or leave it unusable and say why.
// The parent check is the part worth keeping: the composition below adds the
// head pose to the FIRST PERSON CAMERA's world rotation, which is only the
// beam's own base while the component hangs off that camera.
void ResolveTorch(const player_rig::Snapshot& rig) {
    g_pawn = rig.Pawn;
    g_torch = 0;
    g_turned = false;
    g_written = ue::FRotator{0.0, 0.0, 0.0};

    ue_reflect::FieldInfo flashlight;
    if (!ue_reflect::FindPropertyInChain(ue_call::ClassOf(rig.Pawn), "Flashlight", flashlight) ||
        flashlight.TypeName != "ObjectProperty") {
        Log::Line("torch: %s has no Flashlight component - the beam stays on the aim",
                  ue::ObjectName(ue_call::ClassOf(rig.Pawn)).c_str());
        return;
    }
    std::uintptr_t component = 0;
    if (!ue::SafeReadPtr(rig.Pawn + flashlight.Offset, component) || !component) return;

    ue_reflect::FieldInfo attachParent;
    std::uintptr_t parent = 0;
    if (!ue_reflect::FindPropertyInChain(ue_call::ClassOf(component), "AttachParent",
                                         attachParent) ||
        !ue::SafeReadPtr(component + attachParent.Offset, parent)) {
        Log::Line("torch: %s has no readable AttachParent - the beam stays on the aim",
                  ue::ObjectName(ue_call::ClassOf(component)).c_str());
        return;
    }
    if (parent != rig.Camera) {
        Log::Line("torch: the beam hangs off %s, not the first person camera - the beam stays "
                  "on the aim", ue::ObjectName(parent).c_str());
        return;
    }

    g_torch = component;
    Log::Line("torch: %s on %s follows the head at %.2fx", ue::ObjectName(component).c_str(),
              ue::ObjectName(rig.Pawn).c_str(), g_multiplier);
}

// True when there is a torch to write to this frame.
bool Ready(const player_rig::Snapshot& rig) {
    if (!g_followsHead || !rig.Pawn) return false;
    if (!g_haveSetter) {
        if (!g_retry.Due() || !ue_vm::Ready()) return false;
        g_haveSetter = g_setRelativeRotation.Resolve(
            "SceneComponent", "K2_SetRelativeRotation",
            {{"NewRotation", sizeof(ue4::FRotator)}});
        if (!g_haveSetter) return false;
    }
    if (rig.Pawn != g_pawn) ResolveTorch(rig);
    return g_torch != 0;
}

// bSweep and bTeleport stay false: the frame is zeroed and a light component
// has no collision to sweep and no physics body to teleport. Verified in game
// against the same call made with both set.
void Write(const ue::FRotator& relative) {
    ue_call::Frame frame(g_setRelativeRotation);
    frame.Set(0, ue4::FromCore(relative));
    if (!frame.Call(g_torch)) {
        Log::Line("torch: K2_SetRelativeRotation faulted - dropping the beam until the pawn "
                  "is read again");
        g_pawn = 0;
        g_torch = 0;
        g_turned = false;
        return;
    }
    g_written = relative;
}

}  // namespace

void Configure(bool followsHead, float multiplier) {
    g_followsHead = followsHead;
    g_multiplier = multiplier;
    g_state = followsHead ? "unresolved" : "off";
    if (!followsHead) {
        Log::Line("torch: LightFollowsHead is off - the beam stays on the weapon's aim");
        return;
    }
    Log::Line("torch: the beam will follow the head at %.2fx", multiplier);
}

void Apply(const player_rig::Snapshot& rig, double yaw, double pitch, double roll,
           bool worldSpaceYaw) {
    if (!g_followsHead) return;
    // An unreadable parent transform leaves the beam where it is: composing
    // onto a rotation that did not read would point it anywhere.
    if (Ready(rig) && rig.FirstPersonCamera.Valid) {
        const ue::FRotator parent = ue::QuatToRotator(rig.FirstPersonCamera.Rotation);
        const ue::FRotator relative =
            BeamRelative(parent, yaw, pitch, roll, worldSpaceYaw, g_multiplier);
        if (!g_turned || Differs(relative, g_written)) Write(relative);
        g_turned = g_torch != 0;
    }
    g_state = g_turned ? "following" : "unresolved";
}

void Center(const player_rig::Snapshot& rig) {
    if (!g_followsHead) return;
    if (Ready(rig) && g_turned) {
        Write(ue::FRotator{0.0, 0.0, 0.0});
        g_turned = false;
    }
    g_state = g_torch ? "aim" : "unresolved";
}

const char* StateName() { return g_state; }

}  // namespace t2_ht::torch_aim
