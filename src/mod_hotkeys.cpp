// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "mod_hotkeys.h"

#include <algorithm>
#include <memory>
#include <vector>

#include <windows.h>

#include "ads.h"
#include "logging.h"
#include "view_hook.h"

#include "cameraunlock/input/chord_hotkeys.h"
#include "cameraunlock/input/hotkey_poller.h"

namespace t2_ht::hotkeys {

namespace {

using cameraunlock::TrackingMode;
using cameraunlock::input::ChordGuarded;
using cameraunlock::input::NavGuarded;

// Virtual-key codes. The nav-cluster defaults and the Ctrl+Shift chord cluster
// (T/Y/U/G/H/J) are the fleet-wide bindings from AGENTS.md.
//
// Trepang2 fires its key bindings whether or not Ctrl and Shift are held
// (Ctrl+Shift+R reloads in game), and it binds G to ThrowGrenade, H to
// DualWield and T to ToggleFlashlight by default. So the fleet's Ctrl+Shift+G
// would also throw a grenade and Ctrl+Shift+H would dual wield. The tracking
// mode cycle takes the next free letter in the cluster, J, and the yaw-mode
// toggle, with no free letter left, keeps only its nav-cluster key.
constexpr int kVkEnd    = 0x23;
constexpr int kVkPageUp = 0x21;
constexpr int kVkY      = 0x59;
constexpr int kVkU      = 0x55;
constexpr int kVkJ      = 0x4A;

// How often the poller samples the keyboard, in milliseconds.
constexpr unsigned kPollIntervalMs = 16;

std::unique_ptr<cameraunlock::input::HotkeyPoller> g_poller;
Session* g_session = nullptr;

void ToggleTracking() {
    const bool enabled = !view_hook::TrackingEnabled();
    view_hook::SetTrackingEnabled(enabled);
    Log::Line("hotkey: tracking %s", enabled ? "ON" : "OFF");
}

void CycleTrackingMode() {
    const TrackingMode mode = g_session->CycleMode();
    const char* name = mode == TrackingMode::RotationOnly ? "rotation only"
                     : mode == TrackingMode::PositionOnly ? "position only"
                                                          : "rotation and position";
    Log::Line("hotkey: tracking mode -> %s", name);
}

// The mode the frame walk reads once per frame, so the change lands on the aim
// that is already in progress rather than on the next one. Saved as it is
// cycled, because the choice is the player's and a firefight is a bad place to
// lose it.
void CycleAdsMode() {
    const AdsMode next = NextAdsMode(GetAdsMode());
    SetAdsMode(next);
    config::SaveAdsMode(next);
    Log::Line("hotkey: %s", AdsModeToast(next));
}

void ToggleYawMode() {
    const bool worldSpaceYaw = !view_hook::WorldSpaceYaw();
    view_hook::SetWorldSpaceYaw(worldSpaceYaw);
    Log::Line("hotkey: yaw mode %s", worldSpaceYaw ? "world" : "local");
}

}  // namespace

void Register(const Config& config, Session& session) {
    g_session = &session;
    g_poller = std::make_unique<cameraunlock::input::HotkeyPoller>();

    // Nav-cluster defaults. Suppressed when Ctrl+Shift is held so the chord
    // path is the sole trigger.
    //
    // One action per key. The poller fires EVERY entry bound to a code, so a
    // second action on a code already taken would run alongside the first on a
    // single press - and the two keys the INI does let a player change sit
    // directly under a comment naming End and Page Up, which are fixed. A
    // collision is refused with a line rather than bound anyway.
    std::vector<int> navKeys{kVkEnd, kVkPageUp};
    const auto addNav = [&](int vk, const char* key, void (*action)()) {
        if (std::find(navKeys.begin(), navKeys.end(), vk) != navKeys.end()) {
            Log::Line("hotkey: [Hotkeys] %s=0x%02X is a key another action already has - "
                      "%s is not bound to it this session", key, vk, key);
            return;
        }
        navKeys.push_back(vk);
        g_poller->AddHotkey(vk, NavGuarded(action));
    };

    g_poller->AddHotkey(kVkEnd,    NavGuarded([] { ToggleTracking(); }));
    g_poller->AddHotkey(kVkPageUp, NavGuarded([] { CycleTrackingMode(); }));
    addNav(config.yaw_mode_key, "YawMode", &ToggleYawMode);
    addNav(config.ads_mode_key, "AdsMode", &CycleAdsMode);

    // Ctrl+Shift chord alternatives. No chord for the yaw mode: see the key
    // table above.
    g_poller->AddHotkey(kVkY, ChordGuarded([] { ToggleTracking(); }));
    g_poller->AddHotkey(kVkJ, ChordGuarded([] { CycleTrackingMode(); }));
    g_poller->AddHotkey(kVkU, ChordGuarded([] { CycleAdsMode(); }));

    g_poller->Start(kPollIntervalMs);
}

void Stop() {
    if (g_poller) g_poller->Stop();
}

}  // namespace t2_ht::hotkeys
