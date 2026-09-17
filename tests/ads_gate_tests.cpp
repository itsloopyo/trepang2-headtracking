// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// The verdict walk: whether the head pose reaches the view, and what the frame
// reports about the sights while it decides.
//
// ADS is tested LAST in that walk, so a menu or a dead tracker still
// names its own reason when both are true at once, and no early return may leave
// the sights flag set - a stale flag through a menu would hold the pose to the
// aim it was blended into against a weapon that is not raised.

#include "ads_gate.h"
#include "test_harness.h"

namespace {

using t2_ht::AdsMode;
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

// `paused` closes the gate on the aim and still reports the sights: the gate
// says whether tracking applies, the flag says what the weapon is doing.
void TestPausedClosesTheGateAndStillReportsTheSights() {
    const auto s = DecideTracking(Gameplay(), true, true, true, AdsMode::Paused);
    CHECK(s.verdict == TrackingVerdict::AdsSuspended);
    CHECK(s.aiming);
    // A pose still reaches the camera, because suspending is an ease-out rather
    // than a switch. Dropping it on the falling edge would throw the smoothing
    // state away and swing the view back through the head angle on the way out.
    CHECK(PoseApplies(s.verdict));
}

void TestTrackedModesStayOpenThroughAnAim() {
    for (const AdsMode mode : { AdsMode::Marker, AdsMode::Tracked }) {
        const auto s = DecideTracking(Gameplay(), true, true, true, mode);
        CHECK(s.verdict == TrackingVerdict::Active);
        CHECK(s.aiming);
        CHECK(PoseApplies(s.verdict));
    }
}

void TestHipFireIsActiveInEveryMode() {
    for (const AdsMode mode : { AdsMode::Paused, AdsMode::Marker, AdsMode::Tracked }) {
        const auto s = DecideTracking(Gameplay(), true, true, false, mode);
        CHECK(s.verdict == TrackingVerdict::Active);
        CHECK(!s.aiming);
    }
}

// A menu, a cutscene or a loading screen outranks ADS in the reported
// reason, and clears the sights flag with it.
void TestMenuOutranksAdsAndClearsTheFlag() {
    Verdict gate = Gameplay();
    gate.InGameplay = false;
    for (const AdsMode mode : { AdsMode::Paused, AdsMode::Marker, AdsMode::Tracked }) {
        const auto s = DecideTracking(gate, true, true, true, mode);
        CHECK(s.verdict == TrackingVerdict::NotGameplay);
        CHECK(!s.aiming);
        CHECK(!PoseApplies(s.verdict));
    }
}

// The master toggle outranks everything, and reports its own reason.
void TestMasterToggleOutranksAds() {
    const auto s = DecideTracking(Gameplay(), false, true, true, AdsMode::Tracked);
    CHECK(s.verdict == TrackingVerdict::Disabled);
    CHECK(!s.aiming);
    CHECK(!PoseApplies(s.verdict));
}

// No tracker is not an ADS verdict either, and it must not report the sights.
void TestNoTrackerReportsItsOwnReason() {
    const auto s = DecideTracking(Gameplay(), true, false, true, AdsMode::Tracked);
    CHECK(s.verdict == TrackingVerdict::NoTracker);
    CHECK(!s.aiming);
    CHECK(!PoseApplies(s.verdict));
}

// The state is recomputed from the game every frame rather than latched on an
// edge, so an exit event that never arrives - an aim released while firing, a
// state machine that transitions without one - heals on the next frame instead
// of stranding the player in ADS behaviour.
void TestAdsHealsWithoutAnExitEdge() {
    const auto aimed = DecideTracking(Gameplay(), true, true, true, AdsMode::Paused);
    CHECK(aimed.verdict == TrackingVerdict::AdsSuspended);
    const auto healed = DecideTracking(Gameplay(), true, true, false, AdsMode::Paused);
    CHECK(healed.verdict == TrackingVerdict::Active);
    CHECK(!healed.aiming);
}

// A mode cycled mid-aim is read on the next frame's walk, so it lands on the aim
// that is already in progress rather than on the next one.
void TestCyclingMidAimChangesTheVerdict() {
    CHECK(DecideTracking(Gameplay(), true, true, true, AdsMode::Paused).verdict
          == TrackingVerdict::AdsSuspended);
    CHECK(DecideTracking(Gameplay(), true, true, true, AdsMode::Marker).verdict
          == TrackingVerdict::Active);
    CHECK(DecideTracking(Gameplay(), true, true, true, AdsMode::Tracked).verdict
          == TrackingVerdict::Active);
}

// Every verdict names itself, because the log line is what a player is asked to
// send when tracking "just stops".
void TestEveryVerdictHasAReason() {
    for (const TrackingVerdict v : { TrackingVerdict::Active,
                                     TrackingVerdict::AdsSuspended,
                                     TrackingVerdict::Disabled,
                                     TrackingVerdict::NotGameplay,
                                     TrackingVerdict::NoTracker }) {
        const char* reason = Reason(v);
        CHECK(reason != nullptr && reason[0] != '\0');
    }
}

}  // namespace

int main() {
    TestPausedClosesTheGateAndStillReportsTheSights();
    TestTrackedModesStayOpenThroughAnAim();
    TestHipFireIsActiveInEveryMode();
    TestMenuOutranksAdsAndClearsTheFlag();
    TestMasterToggleOutranksAds();
    TestNoTrackerReportsItsOwnReason();
    TestAdsHealsWithoutAnExitEdge();
    TestCyclingMidAimChangesTheVerdict();
    TestEveryVerdictHasAReason();

    return t2_test::Report();
}
