// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// The verdict walk: whether the head pose reaches the view, and what the frame
// reports about the sights while it decides.
//
// The sights never close the gate. ADS is tested LAST in the walk, so a menu or
// a dead tracker still names its own reason when both are true at once, and no
// early return may leave the sights flag set - a stale flag through a menu would
// hold the lean eased out into hip fire.

#include <initializer_list>

#include "ads_gate.h"
#include "test_harness.h"

namespace {

using t2_ht::DecideTracking;
using t2_ht::PoseApplies;
using t2_ht::Reason;
using t2_ht::TrackingVerdict;
using t2_ht::game_state::Verdict;

// The gate as it reads in gameplay.
Verdict Gameplay() {
    Verdict v;
    v.InGameplay = true;
    v.Why = t2_ht::game_state::Blocker::None;
    return v;
}

// Raising the sights leaves tracking on and reports the sights.
void TestAnAimKeepsTrackingOn() {
    const auto s = DecideTracking(Gameplay(), true, true, true);
    CHECK(s.verdict == TrackingVerdict::Active);
    CHECK(s.aiming);
    CHECK(PoseApplies(s.verdict));
}

void TestHipFireIsActive() {
    const auto s = DecideTracking(Gameplay(), true, true, false);
    CHECK(s.verdict == TrackingVerdict::Active);
    CHECK(!s.aiming);
}

// A menu, a cutscene or a loading screen outranks ADS in the reported
// reason, and clears the sights flag with it.
void TestMenuOutranksAdsAndClearsTheFlag() {
    Verdict gate = Gameplay();
    gate.InGameplay = false;
    const auto s = DecideTracking(gate, true, true, true);
    CHECK(s.verdict == TrackingVerdict::NotGameplay);
    CHECK(!s.aiming);
    CHECK(!PoseApplies(s.verdict));
}

// The master toggle outranks everything, and reports its own reason.
void TestMasterToggleOutranksAds() {
    const auto s = DecideTracking(Gameplay(), false, true, true);
    CHECK(s.verdict == TrackingVerdict::Disabled);
    CHECK(!s.aiming);
    CHECK(!PoseApplies(s.verdict));
}

void TestNoTrackerReportsItsOwnReason() {
    const auto s = DecideTracking(Gameplay(), true, false, true);
    CHECK(s.verdict == TrackingVerdict::NoTracker);
    CHECK(!s.aiming);
    CHECK(!PoseApplies(s.verdict));
}

// Every verdict names itself, because the log line is what a player is asked to
// send when tracking "just stops".
void TestEveryVerdictHasAReason() {
    for (const TrackingVerdict v : { TrackingVerdict::Active,
                                     TrackingVerdict::Disabled,
                                     TrackingVerdict::NotGameplay,
                                     TrackingVerdict::NoTracker }) {
        const char* reason = Reason(v);
        CHECK(reason != nullptr && reason[0] != '\0');
    }
}

}  // namespace

int main() {
    TestAnAimKeepsTrackingOn();
    TestHipFireIsActive();
    TestMenuOutranksAdsAndClearsTheFlag();
    TestMasterToggleOutranksAds();
    TestNoTrackerReportsItsOwnReason();
    TestEveryVerdictHasAReason();

    return t2_test::Report();
}
