// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "config.h"

#include <cerrno>
#include <cstdio>
#include <string>

#include "legacy_config/legacy_config.h"
#include "logging.h"

namespace t2_ht::config {

namespace {

constexpr const char* kIniName = "HeadTracking.ini";

std::string IniPath(const std::string& exe_dir) { return exe_dir + "\\" + kIniName; }

}  // namespace

void Load(const std::string& exe_dir, Config& out) {
    legacy::Config read;
    legacy::Load(exe_dir, read);

    out.udp_port = read.udp_port;
    out.local_smoothing = read.local_smoothing;
    out.remote_smoothing = read.remote_smoothing;
    out.yaw_mode_key = read.yaw_mode_key;
    out.world_space_yaw = read.world_space_yaw;
    out.collision_enabled = read.collision_enabled;
    out.collision_margin = read.collision_margin;
    out.collision_channel = read.collision_channel;
    out.aim_trace_channel = read.aim_trace_channel;
    out.collision_release_smoothing = read.collision_release_smoothing;
    out.light_follows_head = read.light_follows_head;
    out.light_multiplier = read.light_multiplier;
    out.dev_commands = read.dev_commands;
}

void WriteDefaultIfMissing(const std::string& exe_dir) {
    const std::string path = IniPath(exe_dir);
    const Config d{};
    FILE* f = std::fopen(path.c_str(), "wbx");
    if (!f) {
        if (errno != EEXIST)
            Log::Line("config: could not write %s (errno %d)", path.c_str(), errno);
        return;
    }

    std::fprintf(f,
        "; Trepang2 Head Tracking\r\n"
        "; Delete this file to get the defaults back.\r\n"
        "\r\n"
        "[Network]\r\n"
        "; UDP port the tracker sends to. 4242 is the OpenTrack default.\r\n"
        "Port=%d\r\n"
        "\r\n"
        "[Tracking]\r\n"
        "; Smoothing, 0.0 (none) to 1.0 (heaviest). LocalSmoothing applies to a\r\n"
        "; tracker sending to 127.0.0.1; RemoteSmoothing to any other address,\r\n"
        "; including this PC's own LAN address.\r\n"
        "LocalSmoothing=%.2f\r\n"
        "RemoteSmoothing=%.2f\r\n"
        "\r\n"
        "[General]\r\n"
        "; 1 = head yaw turns about the world's up axis (horizon stays level).\r\n"
        "; 0 = about the camera's own up axis. Page Down toggles this for the\r\n"
        "; session.\r\n"
        "WorldSpaceYaw=%d\r\n"
        "\r\n"
        "[Camera]\r\n"
        "; Stop a positional lean from putting the view inside walls.\r\n"
        "CollisionEnabled=%d\r\n"
        "; Distance held off a surface, in centimetres (5 to 40).\r\n"
        "CollisionMargin=%.1f\r\n"
        "\r\n"
        "[Light]\r\n"
        "; 1 = the torch points where you are looking instead of where the\r\n"
        "; weapon is aiming.\r\n"
        "LightFollowsHead=%d\r\n"
        "; How far the beam turns for a given head turn, 0 to 5. 1.5 leads the\r\n"
        "; view, so the light reaches what you turned to look at; 1.0 matches\r\n"
        "; the view; 0 leaves the beam on the aim.\r\n"
        "LightMultiplier=%.2f\r\n"
        "\r\n"
        "[Hotkeys]\r\n"
        "; Virtual-key codes. End (toggle tracking), Page Up (cycle tracking\r\n"
        "; mode) and the Ctrl+Shift chords (Y, J) are fixed.\r\n"
        "YawMode=0x%02X\r\n",
        d.udp_port, d.local_smoothing, d.remote_smoothing,
        d.world_space_yaw ? 1 : 0, d.collision_enabled ? 1 : 0,
        d.collision_margin, d.light_follows_head ? 1 : 0, d.light_multiplier,
        d.yaw_mode_key);
    std::fclose(f);
    Log::Line("config: wrote default %s", path.c_str());
}

}  // namespace t2_ht::config
