// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "ads.h"
#include "ads_gate.h"

// What the sights do to the head pose for one frame.
//
// The view hook hands over this frame's absolute pose at the engine boundary and
// gets back the pose to apply. Everything the ADS cycle changes about the camera
// happens here: the transition, the entry-relative pose the tracked modes feed,
// and the one axis neither of them touches.
//
// The transition and the entry pose are core's (cameraunlock/ads/), so this file
// owns only the order they are asked in and the state that has to survive from
// one frame to the next.
//
// The order the render caller drives it in, once per engine frame:
//
//     const bool aiming = ads_state::IsAimingDownSights(controller);
//     const TrackingState s = DecideTracking(gate, enabled, havePose, aiming, GetAdsMode());
//     if (!PoseApplies(s.verdict)) { ads_pose::Reset(); return; }
//     const auto frame = ads_pose::Advance(s, live, absolute, GetTickCount64());
//     // ... apply frame.Pose at the camera boundary ...
//     if (GetAdsMode() == AdsMode::Marker) {
//         float dx = 0.0f, dy = 0.0f;
//         const bool valid = AimProjection::GetScreenOffset(dx, dy);
//         ads_marker::PublishAdsMarker(ads_marker::DecideMarker(
//             s.aiming, AdsMode::Marker, ads_marker::EnsureAdsMarker(), valid, ndcX, ndcY));
//     }
namespace t2_ht::ads_pose {

// The pose for this frame, and what it was decided from.
struct Result {
    AdsEntryPose::Pose Pose;
    // 1 at the hip, 0 with the sights fully up. Exposed because `paused` has to
    // keep feeding the camera until this reaches 0 - the gate closing is an
    // ease-out, not a switch.
    float Scale = 1.0f;
};

// Advance one frame. `state` is the verdict walk's answer, `live` says the
// rotation is a real tracker sample rather than the nothing a suppressed frame
// publishes, `absolute` is the frame's head pose in engine degrees and engine
// position units, and `nowMs` is the caller's clock.
//
// The mode is read live rather than passed in at startup, so a cycle mid-aim
// lands on the aim already in progress.
Result Advance(const TrackingState& state, bool live,
               const AdsEntryPose::Pose& absolute, unsigned long long nowMs);

// Drop the transition and the pose the sights came up on. Called on every frame
// tracking is suppressed - menu, match, loading, master toggle, tracker dropout
// - so the next aim re-enters from where the head is then rather than against a
// pose from before the suppression.
void Reset();

}  // namespace t2_ht::ads_pose
