// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <string>

// The pre-canonical HeadTracking.ini reader, frozen. It reads a file the way the
// last build before the canonical config format did, so a player's old file is
// converted as that build read it. Never edit anything in this folder: the
// differential test in tests/config_differential/ pins every file here by hash.
//
// Frozen from src/config.h and src/config.cpp at 9d691b8, the commit before the
// canonical config conversion, with three changes: it fills this frozen copy of
// that commit's Config and its defaults instead of the runtime type, it writes
// nothing, and it lives in namespace t2_ht::legacy. The light multiplier's
// default and upper bound are written as the literals cameraunlock-core's
// kDefaultLightMultiplier and kMaxLightMultiplier held then (1.5 and 5), so a
// later change to core cannot move what an old file means.
namespace t2_ht::legacy {

struct Config {
    // UDP port the tracker sends to. 4242 is the OpenTrack default.
    int udp_port = 4242;

    // Smoothing for a tracker on this machine (loopback) and for one on another
    // device on the network. 0 = none, 1 = heaviest.
    float local_smoothing = 0.0f;
    float remote_smoothing = 0.15f;

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
    float light_multiplier = 1.5f;

    // Dev only.
    bool dev_commands = false;
};

// Fill `out` from the INI; absent keys keep their defaults, out-of-range values
// fall back to the default and say so in the log.
void Load(const std::string& exe_dir, Config& out);

}  // namespace t2_ht::legacy
