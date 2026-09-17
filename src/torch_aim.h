// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cameraunlock/effects/head_follow_light.h>
#include <cameraunlock/unreal/ue_math.h>

#include "camera_boundary.h"
#include "player_rig.h"

// Point the player's torch where the head is looking rather than where the
// weapon is aiming.
//
// BasePlayer::Flashlight is a SpotLightComponent attached to
// FirstPersonCameraComponent with no socket and an identity relative rotation,
// so the beam leaves along the parent's world forward - the clean aim, which is
// the one thing head tracking deliberately moves the view away from. Turning it
// is therefore one write of that relative rotation and nothing else: no light
// to find in the object table, no world transform to put back, and no second
// component once the game has built the pawn.
//
// The lead and its bound are the fleet's, from
// cameraunlock/effects/head_follow_light.h. Roll is scaled with the other two
// axes for the reason given there.
//
// The write PERSISTS: the game ships the component at identity and never writes
// that field again, so the beam stays where it was put until this module puts it
// back. That is what makes Center() load-bearing rather than tidiness, and it is
// also the one piece of head tracking that outlives the frame it was applied in:
// everything else the mod moves is written into the view the renderer is handed
// and is gone by the time anything else looks.
namespace t2_ht::torch_aim {

// The relative rotation that puts the beam on the head, given the torch's
// parent in world space and the head pose the camera was given this frame.
// `yaw`/`pitch`/`roll` are the pose BEFORE the multiplier; the scaling happens
// here so the beam and the view cannot be composed from different numbers.
//
// Pure: no engine reads, so tests/torch_aim_tests.cpp exercises it.
inline cameraunlock::unreal::FRotator BeamRelative(const cameraunlock::unreal::FRotator& parent,
                                                  double yaw, double pitch, double roll,
                                                  bool worldSpaceYaw, float multiplier) {
    namespace ue = ::cameraunlock::unreal;
    const cameraunlock::effects::HeadEuler led = cameraunlock::effects::ScaleHeadEuler(
        {static_cast<float>(yaw), static_cast<float>(pitch), static_cast<float>(roll)}, multiplier);

    // The camera's own composition, on the beam's own parent. Same function,
    // same order, same signs, so the beam and the view cannot end up disagreeing
    // about which way the head turned.
    ue::FRotator beam = parent;
    camera_boundary::ApplyHeadPose(beam, led.yaw, led.pitch, led.roll, worldSpaceYaw);

    // Unreal composes a child as ParentWorld * Relative, so the relative
    // rotation that lands the beam on `beam` is the parent turned back out of it.
    const ue::FQuat4d parentQ = ue::QuatFromEulerDeg(parent.Pitch, parent.Yaw, parent.Roll);
    const ue::FQuat4d beamQ = ue::QuatFromEulerDeg(beam.Pitch, beam.Yaw, beam.Roll);
    return ue::QuatToRotator(ue::QuatMul(ue::QuatInv(parentQ), beamQ));
}

// Read the config once, before the first frame.
void Configure(bool followsHead, float multiplier);

// Game thread, once per rendered frame, with the pose the camera was given.
void Apply(const player_rig::Snapshot& rig, double yaw, double pitch, double roll,
           bool worldSpaceYaw);

// Put the beam back on the game's own aim. Every frame that applies no pose
// calls this - menus, loading, death, tracking toggled off, the sights up in
// the paused ADS mode.
void Center(const player_rig::Snapshot& rig);

// For the heartbeat: off, aim, following, or why it is none of those.
const char* StateName();

}  // namespace t2_ht::torch_aim
