// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <string>

// The oracle: the HeadTracking.ini reader of the newest published build, v0.2.0
// (bc82b42) against cameraunlock-core 76304a2, compiled only into this test.
// oracle/src/ holds that build's config.h, config.cpp, ads.h and logging.h, and
// oracle/core/ the core headers they include that core has changed or removed
// since, all byte for byte as the tag has them (provenance in
// differential_tests.cpp). oracle_api.cpp compiles them with their namespaces
// renamed, so they link beside today's code.
//
// v0.1.0 (8e560f4, core c480d8a) read the file with the same code: its
// src/config.cpp, config.h, ads.h and logging.h and every core file above are
// byte-identical to v0.2.0's, so this oracle stands for both published builds.
namespace t2_oracle {

// t2_ht::Config as v0.2.0 declared it, field for field.
struct PublishedConfig {
    int udp_port;
    float local_smoothing;
    float remote_smoothing;
    // cameraunlock::ads::AdsMode at 76304a2: 0 paused, 1 marker, 2 tracked.
    int ads_mode;
    int ads_mode_key;
    int yaw_mode_key;
    bool world_space_yaw;
    bool collision_enabled;
    float collision_margin;
    int collision_channel;
    int aim_trace_channel;
    float collision_release_smoothing;
    bool light_follows_head;
    float light_multiplier;
    bool dev_commands;
};

// The published config::Load on a default Config, as its bootstrap called it.
PublishedConfig Load(const std::string& exe_dir);

// The published config::WriteDefaultIfMissing: that build's first-run file.
void WriteDefaultIfMissing(const std::string& exe_dir);

}  // namespace t2_oracle
