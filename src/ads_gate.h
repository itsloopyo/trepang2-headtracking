// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "game_state.h"

// Whether the head pose reaches the view this frame, and why not when it does
// not.
//
// Kept out of the render hook as a pure function so the walk can be exercised
// without the game. Every one of its answers is a frame the player either sees
// their head in or does not.
//
// game_state.h owns the gate that reads the live process. This file is the order
// it is asked in, plus the master toggle, the tracker and the sights. The sights
// never close the gate: head tracking carries straight on through an aim, and
// the only thing they change is the lean (ads_pose.h).
namespace t2_ht {

enum class TrackingVerdict {
    // The head pose is applied.
    Active,
    // The master toggle is off.
    Disabled,
    // A menu, the pause screen, a cutscene, photo mode, death or a loading screen.
    NotGameplay,
    // The tracker has published nothing this frame.
    NoTracker,
};

struct TrackingState {
    TrackingVerdict verdict = TrackingVerdict::NotGameplay;
    // The sights are up, polled from the game this frame. What ads_pose eases
    // the lean out on.
    bool aiming = false;
};

// ADS is tested LAST, so a menu or a dead tracker still reports its own reason
// when both are true at once - and every earlier return leaves `aiming` false,
// because a stale flag through a menu would hold the lean eased out into hip
// fire.
inline TrackingState DecideTracking(const game_state::Verdict& gate,
                                    bool trackingEnabled, bool havePose, bool aiming) {
    TrackingState s;
    if (!trackingEnabled) {
        s.verdict = TrackingVerdict::Disabled;
        return s;
    }
    if (!gate.InGameplay) {
        s.verdict = TrackingVerdict::NotGameplay;
        return s;
    }
    if (!havePose) {
        s.verdict = TrackingVerdict::NoTracker;
        return s;
    }
    s.aiming = aiming;
    s.verdict = TrackingVerdict::Active;
    return s;
}

inline bool PoseApplies(TrackingVerdict verdict) { return verdict == TrackingVerdict::Active; }

// One line for the log and the heartbeat. Never null.
inline const char* Reason(TrackingVerdict verdict) {
    switch (verdict) {
        case TrackingVerdict::Active:          return "gameplay";
        case TrackingVerdict::Disabled:        return "tracking toggled off";
        case TrackingVerdict::NotGameplay:     return "menu or loading";
        case TrackingVerdict::NoTracker:       return "no tracker data";
    }
    return "unknown";
}

}  // namespace t2_ht
