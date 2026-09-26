// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <string>

#include "cameraunlock/config/config_concepts.g.h"
#include "cameraunlock/config/config_owner.h"
#include "cameraunlock/config/legacy_import.h"
#include "cameraunlock/effects/head_follow_light.h"
#include "cameraunlock/math/smoothing_utils.h"
#include "cameraunlock/tracking/tracking_mode.h"

namespace t2_ht {

struct Config {
    // UDP port the tracker sends to. 4242 is the OpenTrack default.
    int udp_port = 4242;
    bool enable_on_startup = true;

    // Smoothing for a tracker on this machine (loopback) and for one on another
    // device on the network. 0 = none, 1 = heaviest.
    float local_smoothing = static_cast<float>(cameraunlock::math::kDefaultLocalSmoothing);
    float remote_smoothing = static_cast<float>(cameraunlock::math::kDefaultRemoteSmoothing);

    // True: head yaw turns about the world up axis (horizon stays level).
    bool world_space_yaw = true;

    // The tracking mode at startup, as the pair the mode hotkey saves.
    bool rotation_enabled = true;
    bool position_enabled = true;

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

    std::string toggle_key =
        cameraunlock::config::schema::ConceptTraits<cameraunlock::config::schema::Concept::ToggleKey>::kCanonicalDefault;
    // Trepang2 fires its own key bindings whether or not Ctrl and Shift are held
    // (Ctrl+Shift+R reloads), and binds G to ThrowGrenade and H to DualWield. So
    // the fleet's Ctrl+Shift+G would also throw a grenade and Ctrl+Shift+H would
    // dual wield: the mode cycle takes J, the next free letter of the chord
    // cluster, and the yaw toggle keeps only Page Down, as every earlier build did.
    std::string cycle_tracking_mode_key = "PageUp, Ctrl+Shift+J";
    std::string yaw_mode_key = "PageDown";

    // Dev only.
    bool dev_commands = false;
};

}  // namespace t2_ht

// CameraUnlock.ini, next to the game exe, in cameraunlock-core's canonical
// config format. One ConfigOwner reads and writes it; nothing else in the mod
// touches it. HeadTracking.ini, the file the builds before it read, is imported
// once while CameraUnlock.ini is absent and is never written.
namespace t2_ht::config {

// The rows of CameraUnlock.ini.
cameraunlock::config::ConfigTable<Config> Table();

// What the renderer writes above the rows.
cameraunlock::config::RenderHeader Header();

// HeadTracking.ini through the frozen reader in legacy_config/, mapped into
// Config.
cameraunlock::config::LegacyImport<Config> Import();

// The owner's options for CameraUnlock.ini in `exe_dir`, with HeadTracking.ini
// beside it as the legacy file and Defaults.ini where `defaults` says.
cameraunlock::config::ConfigOwnerOptions<Config> OwnerOptions(const std::wstring& exe_dir,
                                                              cameraunlock::config::DefaultsFile defaults);

// Reads, imports or creates CameraUnlock.ini in `exe_dir`, logs what the owner
// reports, and returns the settings the session runs on. Call once, from the
// bootstrap thread, with the log open. `exe_dir` must be a full path, and
// `defaults` is DefaultsFile::PerUser() in the mod.
Config Load(const std::wstring& exe_dir, cameraunlock::config::DefaultsFile defaults);

// Save the value a hotkey has just applied. The session keeps it whether or not
// the save succeeds; a failed save is logged. Called from the hotkey thread.
void SaveWorldSpaceYaw(bool world_space_yaw);
void SaveTrackingMode(cameraunlock::TrackingMode mode);

}  // namespace t2_ht::config
