// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "config.h"

#include <cctype>

#include <cerrno>
#include <cstdio>
#include <string>

#include <windows.h>

#include "logging.h"

#include "cameraunlock/config/ini_reader.h"
#include "cameraunlock/config/value_guards.h"
#include "cameraunlock/protocol/port_utils.h"

namespace t2_ht::config {

namespace {

constexpr const char* kIniName = "HeadTracking.ini";

std::string g_iniPath;

std::string IniPath(const std::string& exe_dir) { return exe_dir + "\\" + kIniName; }

int ReadVirtualKey(const cameraunlock::IniReader& ini, const char* key, int fallback) {
    const int vk = ini.ReadHex("Hotkeys", key, fallback);
    if (cameraunlock::config::IsBindableVirtualKey(vk)) return vk;
    Log::Line("config: Hotkeys.%s=0x%X is not a key this mod can bind - using 0x%02X",
              key, vk, fallback);
    return fallback;
}

// The whole value has to parse, and the result has to be a real number in
// range. IniReader::ReadFloatInRange is neither: it parses a PREFIX, so a
// European decimal comma ("0,15") reads back as 0.0 and passes every check
// silently, and its range test is phrased as a rejection, so a NaN - which
// strtod accepts from a literal "nan" - fails both comparisons and is returned
// as valid. A NaN smoothing value or collision margin reaches the camera as a
// NaN rotation written every frame with nothing in the log.
void ReadFloat(const cameraunlock::IniReader& ini, const char* section, const char* key,
               float lo, float hi, float& value) {
    const float fallback = value;
    const std::string raw = cameraunlock::config::ReadRawValue(ini, section, key);
    if (raw.empty()) return;  // absent, empty or all comment: keep the default

    float parsed = 0.0f;
    if (!cameraunlock::config::ParseFloatStrict(raw, parsed)) {
        Log::Line("config: %s.%s=%s is not a number - using %.3f. Use a dot for the "
                  "decimal point.", section, key, raw.c_str(), fallback);
        return;
    }
    // Phrased as a range test rather than its negation, so a non-finite value,
    // which fails every comparison, is rejected instead of passed through.
    if (!(parsed >= lo && parsed <= hi)) {
        Log::Line("config: %s.%s=%.3f is outside %.2f..%.2f - using %.3f",
                  section, key, parsed, lo, hi, fallback);
        return;
    }
    value = parsed;
}

// IniReader::ReadBool compares the WHOLE value against true/1/yes, and
// GetPrivateProfileString does not strip an inline comment, so
// `CollisionEnabled=0 ; walls are fine` matches nothing and silently keeps the
// default - the user's edit discarded with the log printing the default back as
// though it had been read. Same route as the floats: take the raw value, cut
// the comment, compare the token.
void ReadBool(const cameraunlock::IniReader& ini, const char* section, const char* key,
              bool& value) {
    const std::string raw = cameraunlock::config::ReadRawValue(ini, section, key);
    if (raw.empty()) return;
    std::string token;
    for (const char c : raw) token += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (token == "1" || token == "true" || token == "yes" || token == "on") value = true;
    else if (token == "0" || token == "false" || token == "no" || token == "off") value = false;
    else
        Log::Line("config: %s.%s=%s is not a yes/no value - using %d", section, key, raw.c_str(),
                  value ? 1 : 0);
}

void ReadInt(const cameraunlock::IniReader& ini, const char* section, const char* key,
             int lo, int hi, int& value) {
    const int fallback = value;
    if (!ini.ReadIntInRange(section, key, value, lo, hi, fallback)) {
        Log::Line("config: %s.%s=%d is outside %d..%d - using %d", section, key, value, lo, hi,
                  fallback);
        value = fallback;
    }
}

}  // namespace

void Load(const std::string& exe_dir, Config& out) {
    g_iniPath = IniPath(exe_dir);

    cameraunlock::IniReader ini;
    if (!ini.Open(g_iniPath)) {
        Log::Line("config: no %s next to the game exe - using defaults", kIniName);
        return;
    }

    bool port_valid = false;
    out.udp_port = cameraunlock::NormalizeUdpPort(
        ini.ReadInt("Network", "Port", out.udp_port),
        static_cast<uint16_t>(out.udp_port), port_valid);
    if (!port_valid)
        Log::Line("config: Network.Port is outside 1024-65535 - using %d", out.udp_port);

    ReadFloat(ini, "Tracking", "LocalSmoothing", 0.0f, 1.0f, out.local_smoothing);
    ReadFloat(ini, "Tracking", "RemoteSmoothing", 0.0f, 1.0f, out.remote_smoothing);

    out.ads_mode = ParseAdsMode(
        ini.ReadString("View", "AdsMode", AdsModeValue(kDefaultAdsMode)).c_str());
    ReadBool(ini, "General", "WorldSpaceYaw", out.world_space_yaw);

    out.ads_mode_key = ReadVirtualKey(ini, "AdsMode", out.ads_mode_key);
    out.yaw_mode_key = ReadVirtualKey(ini, "YawMode", out.yaw_mode_key);

    ReadBool(ini, "Camera", "CollisionEnabled", out.collision_enabled);
    ReadFloat(ini, "Camera", "CollisionMargin", 5.0f, 40.0f, out.collision_margin);
    ReadInt(ini, "Camera", "CollisionChannel", 0, 31, out.collision_channel);
    ReadFloat(ini, "Camera", "CollisionReleaseSmoothing", 0.0f, 1.0f,
              out.collision_release_smoothing);
    ReadInt(ini, "Camera", "AimTraceChannel", 0, 31, out.aim_trace_channel);

    ReadBool(ini, "Light", "LightFollowsHead", out.light_follows_head);
    ReadFloat(ini, "Light", "LightMultiplier", 0.0f,
              cameraunlock::effects::kMaxLightMultiplier, out.light_multiplier);

    ReadBool(ini, "Dev", "DevCommands", out.dev_commands);

    Log::Line("config: %s loaded (udpPort=%d, smoothing local=%.2f remote=%.2f, adsMode=%s, "
              "yaw=%s, collision=%d margin=%.1f channel=%d, aimChannel=%d, "
              "torch=%d x%.2f)",
              kIniName, out.udp_port, out.local_smoothing, out.remote_smoothing,
              AdsModeValue(out.ads_mode), out.world_space_yaw ? "world" : "local",
              out.collision_enabled ? 1 : 0, out.collision_margin, out.collision_channel,
              out.aim_trace_channel, out.light_follows_head ? 1 : 0, out.light_multiplier);
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
        "[View]\r\n"
        "; What head tracking does while you aim down sights. Insert (or\r\n"
        "; Ctrl+Shift+U) cycles this in game and saves it here.\r\n"
        ";   paused   - tracking stands down for the aim (default)\r\n"
        ";   marker   - tracking stays on, with a crosshair where the rounds land\r\n"
        ";   tracked  - tracking stays on, no crosshair\r\n"
        "AdsMode=%s\r\n"
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
        "; mode) and the Ctrl+Shift chords (Y, J, U) are fixed.\r\n"
        "YawMode=0x%02X\r\n"
        "AdsMode=0x%02X\r\n",
        d.udp_port, d.local_smoothing, d.remote_smoothing, AdsModeValue(kDefaultAdsMode),
        d.world_space_yaw ? 1 : 0, d.collision_enabled ? 1 : 0,
        d.collision_margin, d.light_follows_head ? 1 : 0, d.light_multiplier,
        d.yaw_mode_key, d.ads_mode_key);
    std::fclose(f);
    Log::Line("config: wrote default %s", path.c_str());
}

void SaveAdsMode(AdsMode mode) {
    if (g_iniPath.empty()) return;
    if (!WritePrivateProfileStringA("View", "AdsMode", AdsModeValue(mode), g_iniPath.c_str())) {
        Log::Line("config: could not save AdsMode to %s (error %lu) - the setting applies "
                  "for this session only", g_iniPath.c_str(), GetLastError());
    }
}

}  // namespace t2_ht::config
