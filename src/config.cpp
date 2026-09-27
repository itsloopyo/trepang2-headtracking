// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "config.h"

#include <cstddef>
#include <cstring>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <windows.h>

#include "legacy_config/legacy_config.h"
#include "logging.h"

#include "cameraunlock/config/hotkey_codec.h"
#include "cameraunlock/config/value_codecs.h"

namespace t2_ht::config {

namespace {

namespace cfg = ::cameraunlock::config;
using cfg::schema::Concept;
using cfg::schema::ConceptTraits;

constexpr const wchar_t* kIniName = L"CameraUnlock.ini";
constexpr const wchar_t* kLegacyIniName = L"HeadTracking.ini";

// data/games.json's display_name for trepang2.
constexpr const char* kDisplayName = "Trepang2";

// ETraceTypeQuery holds TraceTypeQuery1 to TraceTypeQuery32.
constexpr double kMaxTraceChannel = 31;

// The keys every build before the canonical format bound in code to the toggle
// and the mode cycle, which it refused as the yaw key.
constexpr int kVkEnd = 0x23;
constexpr int kVkPageUp = 0x21;

std::unique_ptr<cfg::ConfigOwner<Config>> g_owner;

void Save(const char* rows, const std::function<void(Config&)>& change) {
    // No owner when the bootstrap could not read the game directory.
    if (!g_owner) {
        Log::Line("config: %s not saved: CameraUnlock.ini has no known folder this session", rows);
        return;
    }
    const cfg::ConfigSaveResult result = g_owner->Save(change);
    if (result.status != cfg::ConfigSaveStatus::Saved) {
        Log::Line("config: %s %s: %s", rows, cfg::ConfigSaveStatusName(result.status), result.reason.c_str());
    }
    for (const std::string& line : result.log) Log::Line("config: %s", line.c_str());
}

cfg::ImportResult RunImport(const cfg::LegacyInput& input, Config& out) {
    // The frozen reader takes the folder and names the file itself.
    const std::string& path = input.ansi_path;
    constexpr const char* kLegacySuffix = "\\HeadTracking.ini";
    const std::size_t suffix_length = std::strlen(kLegacySuffix);
    if (path.size() < suffix_length ||
        _stricmp(path.c_str() + path.size() - suffix_length, kLegacySuffix) != 0) {
        throw std::invalid_argument("the legacy import reads HeadTracking.ini only, not " + path);
    }
    // The test the frozen reader opens the file with: where it fails, the
    // published build ran on its defaults.
    const bool present = GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES;

    legacy::Config read;
    legacy::Load(path.substr(0, path.size() - suffix_length), read);

    // The shipped values: every field the frozen reader leaves alone keeps these.
    const legacy::Config shipped;
    cfg::LegacyFollowsDefaultsIni follows;
    std::vector<cfg::DroppedValue> dropped;

    out.udp_port = read.udp_port;
    follows.Setting(Concept::UdpPort, read.udp_port, shipped.udp_port);
    // Every earlier build started with head tracking on, in rotation and
    // position, whatever the file said.
    out.enable_on_startup = true;
    follows.NotInLegacy(Concept::EnableOnStartup);
    out.rotation_enabled = true;
    out.position_enabled = true;
    follows.TrackingMode(true);
    // The reader refuses a value that is not a finite number in range, so every
    // float here is finite and inside the concept's range.
    out.local_smoothing = read.local_smoothing;
    follows.Setting(Concept::LocalSmoothing, read.local_smoothing, shipped.local_smoothing);
    out.remote_smoothing = read.remote_smoothing;
    follows.Setting(Concept::RemoteSmoothing, read.remote_smoothing, shipped.remote_smoothing);
    out.world_space_yaw = read.world_space_yaw;
    follows.Setting(Concept::WorldSpaceYaw, read.world_space_yaw, shipped.world_space_yaw);
    out.collision_enabled = read.collision_enabled;
    follows.Setting(Concept::CollisionEnabled, read.collision_enabled, shipped.collision_enabled);
    out.collision_margin = read.collision_margin;
    out.collision_channel = read.collision_channel;
    out.collision_release_smoothing = read.collision_release_smoothing;
    follows.Setting(Concept::CollisionReleaseSmoothing, read.collision_release_smoothing,
                    shipped.collision_release_smoothing);
    out.aim_trace_channel = read.aim_trace_channel;
    out.light_follows_head = read.light_follows_head;
    follows.Setting(Concept::LightFollowsHead, read.light_follows_head, shipped.light_follows_head);
    out.light_multiplier = read.light_multiplier;
    follows.Setting(Concept::LightMultiplier, read.light_multiplier, shipped.light_multiplier);
    out.dev_commands = read.dev_commands;

    // End and the Ctrl+Shift+Y chord were bound in code, and are the fleet's
    // toggle list, so no player chose them. Page Up and Ctrl+Shift+J, also bound
    // in code, are this game's mode list. Only the yaw key was in the file, and
    // the reader keeps it inside 0x01-0xFE. The build refused a yaw key that was
    // End or Page Up, which already had an action, and bound the yaw toggle to
    // nothing.
    out.toggle_key = ConceptTraits<Concept::ToggleKey>::kCanonicalDefault;
    follows.NotInLegacy(Concept::ToggleKey);
    out.cycle_tracking_mode_key = Config{}.cycle_tracking_mode_key;
    if (read.yaw_mode_key == kVkEnd || read.yaw_mode_key == kVkPageUp) {
        out.yaw_mode_key.clear();
    } else {
        out.yaw_mode_key = cfg::LegacyVirtualKeyToBindings(read.yaw_mode_key, "Hotkeys", "YawMode", dropped);
    }

    return present ? cfg::ImportResult::Imported(std::move(dropped), {}, follows.Concepts())
                   : cfg::ImportResult::Absent(std::move(dropped), {}, follows.Concepts());
}

}  // namespace

cfg::ConfigTable<Config> Table() {
    cfg::ConfigTable<Config> table;
    table.Concept<Concept::UdpPort>(&Config::udp_port)
        .Concept<Concept::EnableOnStartup>(&Config::enable_on_startup)
        .Concept<Concept::WorldSpaceYaw>(&Config::world_space_yaw)
        .Writable()
        .Concept<Concept::RotationEnabled>(&Config::rotation_enabled)
        .Writable()
        .Concept<Concept::LocalSmoothing>(&Config::local_smoothing)
        .Concept<Concept::RemoteSmoothing>(&Config::remote_smoothing)
        .Concept<Concept::PositionEnabled>(&Config::position_enabled)
        .Writable()
        .Concept<Concept::CollisionEnabled>(&Config::collision_enabled)
        .Concept<Concept::CollisionMargin>(&Config::collision_margin)
        .Comment("How far the view is held off a wall when you lean into it, in centimetres.\n"
                 "Keep it above 3, the game's near clip distance.")
        .Concept<Concept::CollisionChannel>(&Config::collision_channel)
        .Engine()
        .Concept<Concept::CollisionReleaseSmoothing>(&Config::collision_release_smoothing)
        .Concept<Concept::ToggleKey>(&Config::toggle_key)
        .Concept<Concept::CycleTrackingModeKey>(&Config::cycle_tracking_mode_key)
        .PerGame()
        .Concept<Concept::YawModeKey>(&Config::yaw_mode_key)
        .PerGame()
        .Concept<Concept::LightFollowsHead>(&Config::light_follows_head)
        .Concept<Concept::LightMultiplier>(&Config::light_multiplier)
        .Local("Aim", "AimTraceChannel", &Config::aim_trace_channel, cfg::IntCodec<int>(),
               "Which of the game's collision channels the aim trace tests against, 0 to 31. The\n"
               "trace finds where the shot lands, so the crosshair can sit on that point.")
        .Range(0, kMaxTraceChannel)
        .Engine()
        .Local("Dev", "DevCommands", &Config::dev_commands, cfg::BoolCodec(),
               "For development. true: run the commands in HeadTracking.devcmd beside the game's\n"
               "executable.");
    return table;
}

cfg::RenderHeader Header() {
    cfg::RenderHeader header;
    header.display_name = kDisplayName;
    return header;
}

cfg::LegacyImport<Config> Import() {
    cfg::LegacyImport<Config> import;
    import.run = &RunImport;
    // Every key the frozen reader takes a value from.
    import.keys = {
        {"Network", "Port"},
        {"Tracking", "LocalSmoothing"},
        {"Tracking", "RemoteSmoothing"},
        {"General", "WorldSpaceYaw"},
        {"Hotkeys", "YawMode"},
        {"Camera", "CollisionEnabled"},
        {"Camera", "CollisionMargin"},
        {"Camera", "CollisionChannel"},
        {"Camera", "CollisionReleaseSmoothing"},
        {"Camera", "AimTraceChannel"},
        {"Light", "LightFollowsHead"},
        {"Light", "LightMultiplier"},
        {"Dev", "DevCommands"},
    };
    return import;
}

cfg::ConfigOwnerOptions<Config> OwnerOptions(const std::wstring& exe_dir, cfg::DefaultsFile defaults) {
    cfg::ConfigOwnerOptions<Config> options;
    options.path = exe_dir + L"\\" + kIniName;
    options.table = Table();
    options.import = Import();
    options.legacy_path = exe_dir + L"\\" + kLegacyIniName;
    options.header = Header();
    options.defaults = std::move(defaults);
    return options;
}

Config Load(const std::wstring& exe_dir, cfg::DefaultsFile defaults) {
    g_owner = std::make_unique<cfg::ConfigOwner<Config>>(OwnerOptions(exe_dir, std::move(defaults)));
    const cfg::ConfigLoadResult<Config> result = g_owner->Load();
    for (const std::string& line : result.log) Log::Line("config: %s", line.c_str());
    if (!result.reason.empty()) Log::Line("config: %s", result.reason.c_str());
    Log::Line("config: %s", cfg::ConfigLoadStatusName(result.status));
    return result.config;
}

void SaveWorldSpaceYaw(bool world_space_yaw) {
    Save("[General] WorldSpaceYaw", [world_space_yaw](Config& c) { c.world_space_yaw = world_space_yaw; });
}

void SaveTrackingMode(cameraunlock::TrackingMode mode) {
    const cameraunlock::TrackingModeChannels channels = cameraunlock::EncodeTrackingMode(mode);
    Save("[General] RotationEnabled and [Position] PositionEnabled", [channels](Config& c) {
        c.rotation_enabled = channels.rotation_enabled;
        c.position_enabled = channels.position_enabled;
    });
}

}  // namespace t2_ht::config
