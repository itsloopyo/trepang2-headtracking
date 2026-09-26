// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "mod_hotkeys.h"

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "hotkey_overlap.h"
#include "logging.h"
#include "view_hook.h"

#include "cameraunlock/input/hotkey_poller.h"
#include "cameraunlock/input/key_binding_registration.h"
#include "cameraunlock/input/key_bindings.h"

namespace t2_ht::hotkeys {

namespace {

using cameraunlock::TrackingMode;
using cameraunlock::input::KeyBinding;

// How often the poller samples the keyboard, in milliseconds.
constexpr unsigned kPollIntervalMs = 16;

std::unique_ptr<cameraunlock::input::HotkeyPoller> g_poller;
Session* g_session = nullptr;

// End changes this session only; EnableOnStartup decides the next one.
void ToggleTracking() {
    const bool enabled = !view_hook::TrackingEnabled();
    view_hook::SetTrackingEnabled(enabled);
    Log::Line("hotkey: tracking %s", enabled ? "ON" : "OFF");
}

// The session's mode is an atomic the render thread reads each frame, so the
// cycle applies it here and then saves it.
void CycleTrackingMode() {
    const TrackingMode mode = g_session->CycleMode();
    const char* name = mode == TrackingMode::RotationOnly ? "rotation only"
                     : mode == TrackingMode::PositionOnly ? "position only"
                                                          : "rotation and position";
    Log::Line("hotkey: tracking mode -> %s", name);
    config::SaveTrackingMode(mode);
}

void ToggleYawMode() {
    const bool worldSpaceYaw = !view_hook::WorldSpaceYaw();
    view_hook::SetWorldSpaceYaw(worldSpaceYaw);
    Log::Line("hotkey: yaw mode %s", worldSpaceYaw ? "world" : "local");
    config::SaveWorldSpaceYaw(worldSpaceYaw);
}

// The table's hotkey codec only lets through a list this parser reads.
std::vector<KeyBinding> Bindings(const char* key, const std::string& list) {
    const cameraunlock::input::KeyBindingsParseResult parsed = cameraunlock::input::ParseKeyBindings(list);
    if (!parsed.ok()) throw std::logic_error(std::string(key) + "='" + list + "': " + parsed.error);
    return parsed.bindings;
}

struct Claim {
    KeyBinding binding;
    const char* key;
};

// Registers the bindings of `list` that no earlier action's binding fires
// together with, and records them in `claims`. The poller runs every action on a
// key, so a clash (a player's YawModeKey=End, or a Defaults.ini ToggleKey
// holding PageUp) would fire two actions on one press; the earlier action keeps
// the key and the log says which binding was left out.
void RegisterUnclaimed(std::vector<Claim>& claims, const char* key, const std::string& list, void (*action)()) {
    std::vector<KeyBinding> kept;
    for (const KeyBinding& binding : Bindings(key, list)) {
        const Claim* clash = nullptr;
        for (const Claim& claim : claims) {
            if (hotkey_overlap::FireTogether(claim.binding, binding)) {
                clash = &claim;
                break;
            }
        }
        if (clash) {
            Log::Line("hotkey: %s's %s fires on the same press as %s's %s - it is not bound this session", key,
                      cameraunlock::input::FormatKeyBindings({binding}).c_str(), clash->key,
                      cameraunlock::input::FormatKeyBindings({clash->binding}).c_str());
            continue;
        }
        kept.push_back(binding);
    }
    for (const KeyBinding& binding : kept) claims.push_back({binding, key});
    cameraunlock::input::RegisterKeyBindings(*g_poller, kept, action);
}

}  // namespace

void Register(const Config& config, Session& session) {
    g_session = &session;
    g_poller = std::make_unique<cameraunlock::input::HotkeyPoller>();

    // Each list holds every key that fires its action, the Ctrl+Shift chord
    // included. Within a list a key without modifiers stays silent while Ctrl and
    // Shift are both held; across lists RegisterUnclaimed keeps one press to one
    // action, in this order.
    std::vector<Claim> claims;
    RegisterUnclaimed(claims, "ToggleKey", config.toggle_key, &ToggleTracking);
    RegisterUnclaimed(claims, "CycleTrackingModeKey", config.cycle_tracking_mode_key, &CycleTrackingMode);
    RegisterUnclaimed(claims, "YawModeKey", config.yaw_mode_key, &ToggleYawMode);
    Log::Line("hotkey: toggle=[%s] cycle tracking mode=[%s] yaw mode=[%s]", config.toggle_key.c_str(),
              config.cycle_tracking_mode_key.c_str(), config.yaw_mode_key.c_str());

    g_poller->Start(kPollIntervalMs);
}

void Stop() {
    if (g_poller) g_poller->Stop();
}

}  // namespace t2_ht::hotkeys
