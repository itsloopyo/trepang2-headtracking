// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

// What the sights do to the head pose for one frame: they ease the lean out.
//
// Rotation is left alone. The mod only rewrites the view the render caller is
// handed; the game still poses the weapon from its own clean camera, and turning
// the drawn view about the eye leaves the weapon's sight line through that eye,
// so the sights stay lined up with the head turned. A lean translates the eye off that
// line, and the weapon is drawn in the same scene pass as the world with no
// separate view the mod could give it, so the lean is scaled by core's AdsFade
// while the sights are up and comes back when they drop.
namespace t2_ht::ads_pose {

// Engine degrees and engine position units, straight from the camera boundary.
struct Pose {
    float yaw = 0.0f;
    float pitch = 0.0f;
    float roll = 0.0f;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

// Advance one frame. `aiming` is this frame's polled sights state and `nowMs`
// the caller's clock. Returns `absolute` with x, y and z scaled by the fade.
Pose Advance(bool aiming, const Pose& absolute, unsigned long long nowMs);

// Drop the transition. Called on every frame the pose is not applied - menu,
// loading, master toggle, tracker dropout - so the next aim starts clean.
void Reset();

}  // namespace t2_ht::ads_pose
