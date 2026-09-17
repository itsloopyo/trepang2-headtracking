// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "ads.h"

// When the mod's own aim marker is on screen. Pure, so the rule can be locked by
// a test.
//
// At the hip the game's crosshair is on screen and the mod moves it, so no
// marker is drawn. With the sights up the game hides its crosshair, so only the
// `marker` ADS mode draws one, and only while the head pose is applied and the
// impact point projects inside the frame. An invalid or off-screen projection
// draws nothing rather than a mark where the rounds are not going.
namespace t2_ht::marker_rule {

inline bool ShowAdsMarker(bool poseApplied, bool aiming, AdsMode mode, bool projectionOnScreen) {
    return poseApplied && aiming && mode == AdsMode::Marker && projectionOnScreen;
}

}  // namespace t2_ht::marker_rule
