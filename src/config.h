// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <string>

#include <cameraunlock/effects/head_follow_light.h>

#include "ads.h"

// HeadTracking.ini, next to the game exe.
namespace t2_ht {

struct Config {
    // UDP port the tracker sends to. 4242 is the OpenTrack default.
    int udp_port = 4242;

    // Smoothing for a tracker on this machine (loopback) and for one on another
    // device on the network. 0 = none, 1 = heaviest.
    float local_smoothing = 0.0f;
    float remote_smoothing = 0.15f;

    AdsMode ads_mode = kDefaultAdsMode;
    int ads_mode_key = 0x2D;  // VK_INSERT
    int yaw_mode_key = 0x22;  // VK_NEXT (Page Down)

    // True: head yaw turns about the world up axis (horizon stays level).
    bool world_space_yaw = true;

    // Keep a lean from putting the eye inside the level.
    bool collision_enabled = true;
    // Standoff from a surface in cm, along its normal; must exceed the near clip
    // plane, which Trepang2's engine config sets to 3cm.
    float collision_margin = 10.0f;
    // ETraceTypeQuery index for the lean trace and the aim trace.
    int collision_channel = 0;
    int aim_trace_channel = 0;
    float collision_release_smoothing = 0.9f;

    // Point the torch where the head is looking rather than where the weapon is
    // aiming, and how far it leads the view. 1.0 matches the view, 0 pins the
    // beam to the aim.
    bool light_follows_head = true;
    float light_multiplier = cameraunlock::effects::kDefaultLightMultiplier;

    // Dev only.
    bool dev_commands = false;
};

}  // namespace t2_ht

namespace t2_ht::config {

// Fill `out` from the INI; absent keys keep their defaults, out-of-range values
// fall back to the default and say so in the log.
void Load(const std::string& exe_dir, Config& out);

// Write a commented default INI unless one already exists.
void WriteDefaultIfMissing(const std::string& exe_dir);

// Persist the ADS mode, leaving every other key alone.
void SaveAdsMode(AdsMode mode);

}  // namespace t2_ht::config
