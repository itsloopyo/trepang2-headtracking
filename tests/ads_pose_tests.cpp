// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// What the sights do to the head pose: the lean eases out, and rotation is left
// exactly as the tracker sent it.
//
// The transition timings are core's (AdsFade) and are not retested here; these
// cases pin what this mod does with the fade's output.

#include "ads_pose.h"
#include "test_harness.h"

#include "cameraunlock/ads/ads_fade.h"

namespace {

using t2_ht::ads_pose::Advance;
using t2_ht::ads_pose::Pose;
using t2_ht::ads_pose::Reset;
using cameraunlock::ads::AdsFade;

Pose MakePose() {
    Pose p;
    p.yaw = -12.0f;
    p.pitch = 5.0f;
    p.roll = 3.0f;
    p.x = 10.0f;
    p.y = -4.0f;
    p.z = 6.0f;
    return p;
}

void CheckRotationUntouched(const Pose& out, const Pose& in) {
    CHECK_NEAR_MSG(out.yaw, in.yaw, 1e-6, "yaw is never faded in ADS");
    CHECK_NEAR_MSG(out.pitch, in.pitch, 1e-6, "pitch is never faded in ADS");
    CHECK_NEAR_MSG(out.roll, in.roll, 1e-6, "roll is never faded in ADS");
}

void TestHipFirePassesThePoseThrough() {
    Reset();
    const Pose in = MakePose();
    const Pose out = Advance(false, in, 1000);
    CheckRotationUntouched(out, in);
    CHECK_NEAR(out.x, in.x, 1e-6);
    CHECK_NEAR(out.y, in.y, 1e-6);
    CHECK_NEAR(out.z, in.z, 1e-6);
}

void TestSightsUpKeepRotationAndDropTheLean() {
    Reset();
    const Pose in = MakePose();
    Advance(true, in, 1000);
    const Pose out = Advance(true, in, 1000 + AdsFade::kLowerMs);
    CheckRotationUntouched(out, in);
    CHECK_NEAR(out.x, 0.0, 1e-6);
    CHECK_NEAR(out.y, 0.0, 1e-6);
    CHECK_NEAR(out.z, 0.0, 1e-6);
}

// Raising the sights does not move the view: the first aiming frame carries the
// same rotation as the hip frame before it.
void TestRaisingTheSightsDoesNotMoveTheView() {
    Reset();
    const Pose in = MakePose();
    const Pose hip = Advance(false, in, 1000);
    const Pose raised = Advance(true, in, 1016);
    CHECK_NEAR(raised.yaw, hip.yaw, 1e-6);
    CHECK_NEAR(raised.pitch, hip.pitch, 1e-6);
    CHECK_NEAR(raised.roll, hip.roll, 1e-6);
}

void TestMidTransitionScalesOnlyTheLean() {
    Reset();
    const Pose in = MakePose();
    Advance(true, in, 1000);
    const Pose out = Advance(true, in, 1000 + AdsFade::kLowerMs / 2);
    CheckRotationUntouched(out, in);
    const float scale = out.x / in.x;
    CHECK_MSG(scale > 0.0f && scale < 1.0f, "halfway down, the lean is partly applied");
    CHECK_NEAR(out.y, in.y * scale, 1e-5);
    CHECK_NEAR(out.z, in.z * scale, 1e-5);
}

// A tap of the aim button: the reversal starts from where the lean is, not from
// either end, so nothing steps.
void TestAReversalContinuesFromWhereTheLeanIs() {
    Reset();
    const Pose in = MakePose();
    Advance(true, in, 1000);
    const Pose down = Advance(true, in, 1000 + AdsFade::kLowerMs / 2);
    const Pose reversed = Advance(false, in, 1000 + AdsFade::kLowerMs / 2);
    CHECK_NEAR_MSG(reversed.x, down.x, 1e-5, "the reversal frame holds the lean where it was");
    const Pose later = Advance(false, in, 1000 + AdsFade::kLowerMs / 2 + 16);
    CHECK_MSG(later.x > reversed.x && later.x < in.x, "and then heads back up from there");
    const Pose back = Advance(false, in, 1000 + AdsFade::kLowerMs + AdsFade::kRaiseMs);
    CHECK_NEAR(back.x, in.x, 1e-6);
}

// A suppressed frame resets the fade, so the next aim starts from the hip
// rather than from a lean left eased out before a menu.
void TestResetStartsFromTheHip() {
    Reset();
    const Pose in = MakePose();
    Advance(true, in, 1000);
    Advance(true, in, 1000 + AdsFade::kLowerMs);
    Reset();
    const Pose out = Advance(false, in, 5000);
    CHECK_NEAR(out.x, in.x, 1e-6);
}

}  // namespace

int main() {
    TestHipFirePassesThePoseThrough();
    TestSightsUpKeepRotationAndDropTheLean();
    TestRaisingTheSightsDoesNotMoveTheView();
    TestMidTransitionScalesOnlyTheLean();
    TestAReversalContinuesFromWhereTheLeanIs();
    TestResetStartsFromTheHip();

    return t2_test::Report();
}
