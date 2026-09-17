// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstdint>

#include <cameraunlock/unreal/ue_math.h>

#include "player_rig.h"

// When head tracking is allowed to touch the view at all. Polled every render
// frame from the live controller, never latched.
//
// Gameplay is the frame drawn from the player character's own first-person
// camera, alive, with no cursor up, the game not paused, no level cutscene
// playing, not in photo mode and no level load in progress. Anything unreadable
// reads as the blocking answer.
//
// Trepang2 has no multiplayer mode (the main menu offers a new game, cheats,
// options, credits and quit, and the combat simulator is played alone), so
// there is no session gate here.
namespace t2_ht::game_state {

enum class Blocker {
    None,
    NoPlayer,
    Dead,
    Loading,
    Cursor,
    Paused,
    Cutscene,
    PhotoMode,
    NotFirstPerson,
};

struct Verdict {
    bool InGameplay = false;
    Blocker Why = Blocker::NoPlayer;
    // How far the drawn view sits from the first-person camera, for the log.
    double ViewOffsetCm = -1.0;
    double ViewAngleDeg = -1.0;
};

// `cleanLocation` / `cleanRotation` are the view the game built this frame,
// before any head pose.
Verdict Evaluate(std::uintptr_t controller, const player_rig::Snapshot& rig,
                 const cameraunlock::unreal::FVector& cleanLocation,
                 const cameraunlock::unreal::FRotator& cleanRotation);

void LogTransitions(const Verdict& v);

const char* BlockerName(Blocker b);

}  // namespace t2_ht::game_state
