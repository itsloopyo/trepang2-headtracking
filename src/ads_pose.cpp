// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "ads_pose.h"

#include "cameraunlock/ads/ads_fade.h"

namespace t2_ht::ads_pose {

namespace {

// Lives on the game thread with the frame walk that drives it.
cameraunlock::ads::AdsFade g_fade;

}  // namespace

Pose Advance(bool aiming, const Pose& absolute, unsigned long long nowMs) {
    const float lean = g_fade.Update(aiming, nowMs);
    Pose out = absolute;
    out.x *= lean;
    out.y *= lean;
    out.z *= lean;
    return out;
}

void Reset() { g_fade.Reset(); }

}  // namespace t2_ht::ads_pose
