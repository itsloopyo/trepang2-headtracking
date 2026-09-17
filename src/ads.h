// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "cameraunlock/ads/ads_blend.h"
#include "cameraunlock/ads/ads_fade.h"
#include "cameraunlock/ads/ads_mode.h"
#include "cameraunlock/ads/entry_pose.h"

// The shared aim-down-sights module, under this mod's namespace.
//
// Trepang2 ships THREE slots (paused / marker / tracked). The game draws its
// crosshair at the hip and hides it when the sights come up: aiming puts the
// player behind the weapon's own irons, or behind a 2D scope overlay on an
// optic, and both are only honest while the eye sits on the sight line that
// head tracking moves it off. The mod's reticle compensation cannot reach a
// crosshair the game has hidden, so the `marker` slot draws one.
namespace t2_ht {

using cameraunlock::ads::AdsEntryPose;
using cameraunlock::ads::AdsFade;
using cameraunlock::ads::AdsMode;
using cameraunlock::ads::AdsModeLabel;
using cameraunlock::ads::AdsModeToast;
using cameraunlock::ads::AdsModeValue;
using cameraunlock::ads::AdsSuspendsTracking;
using cameraunlock::ads::BlendAdsPose;
using cameraunlock::ads::kDefaultAdsMode;
using cameraunlock::ads::NextAdsMode;
using cameraunlock::ads::ParseAdsMode;

// The mode in force right now. Written from the hotkey thread and read once per
// frame on the game thread, so it is an atomic rather than a plain member: a
// mode cycled mid-aim has to land on the aim already in progress, which means
// the frame walk reads it live rather than caching it at startup.
AdsMode GetAdsMode();
void SetAdsMode(AdsMode mode);

}  // namespace t2_ht
