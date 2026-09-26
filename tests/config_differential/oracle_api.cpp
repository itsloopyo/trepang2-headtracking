// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Built with t2_ht and cameraunlock renamed on the command line (CMakeLists.txt),
// so the published reader and the core code it calls are a separate copy from
// today's, and nothing here can resolve to a symbol of the mod under test.

#include "oracle_api.h"

#include "config.h"

namespace t2_oracle {

PublishedConfig Load(const std::string& exe_dir) {
    t2_ht::Config c;
    t2_ht::config::Load(exe_dir, c);
    return PublishedConfig{
        c.udp_port,
        c.local_smoothing,
        c.remote_smoothing,
        static_cast<int>(c.ads_mode),
        c.ads_mode_key,
        c.yaw_mode_key,
        c.world_space_yaw,
        c.collision_enabled,
        c.collision_margin,
        c.collision_channel,
        c.aim_trace_channel,
        c.collision_release_smoothing,
        c.light_follows_head,
        c.light_multiplier,
        c.dev_commands,
    };
}

void WriteDefaultIfMissing(const std::string& exe_dir) { t2_ht::config::WriteDefaultIfMissing(exe_dir); }

}  // namespace t2_oracle
