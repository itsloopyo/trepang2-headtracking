// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Behaviour locks for the rotation the torch is given.
//
// The property that matters is not the relative rotation's own numbers - it is
// that composing them back onto the parent lands the beam exactly where the
// camera's own composition would have put a view. A beam derived from its own
// maths agrees with the view at small single-axis angles and parts company on a
// combined pose, which is the failure these checks exist to catch.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

#include "torch_aim.h"

namespace {

namespace ue = ::cameraunlock::unreal;
using t2_ht::camera_boundary::ApplyHeadPose;
using t2_ht::torch_aim::BeamRelative;

constexpr double kTolerance = 1e-6;
constexpr float kLead = cameraunlock::effects::kDefaultLightMultiplier;

int g_failures = 0;

void CheckNear(double actual, double expected, const char* what, double tolerance = kTolerance) {
    if (std::fabs(actual - expected) <= tolerance) return;
    std::printf("FAIL: %s (expected %.9f, got %.9f)\n", what, expected, actual);
    ++g_failures;
}

// Where the parent ends up once the relative rotation is composed onto it, the
// way Unreal composes a child of an attached component.
ue::FRotator BeamWorld(const ue::FRotator& parent, double yaw, double pitch, double roll,
                       bool worldSpaceYaw, float multiplier) {
    const ue::FRotator rel = BeamRelative(parent, yaw, pitch, roll, worldSpaceYaw, multiplier);
    return ue::QuatToRotator(
        ue::QuatMul(ue::QuatFromEulerDeg(parent.Pitch, parent.Yaw, parent.Roll),
                    ue::QuatFromEulerDeg(rel.Pitch, rel.Yaw, rel.Roll)));
}

// What the camera would have done to the same rotation with the same pose.
ue::FRotator ViewWorld(const ue::FRotator& parent, double yaw, double pitch, double roll,
                       bool worldSpaceYaw, float multiplier) {
    ue::FRotator view = parent;
    ApplyHeadPose(view, yaw * multiplier, pitch * multiplier, roll * multiplier, worldSpaceYaw);
    return view;
}

void CheckAgrees(const ue::FRotator& parent, double yaw, double pitch, double roll,
                 bool worldSpaceYaw, const char* what) {
    const ue::FRotator beam = BeamWorld(parent, yaw, pitch, roll, worldSpaceYaw, kLead);
    const ue::FRotator view = ViewWorld(parent, yaw, pitch, roll, worldSpaceYaw, kLead);
    CheckNear(beam.Pitch, view.Pitch, what);
    CheckNear(beam.Yaw, view.Yaw, what);
    CheckNear(beam.Roll, view.Roll, what);
}

void BeamLandsWhereTheCompositionAsked() {
    const ue::FRotator level{0.0, -90.0, 0.0};
    const ue::FRotator pitched{-32.0, 145.0, 0.0};

    CheckAgrees(level, 12.0, 0.0, 0.0, true, "world yaw, level parent, yaw only");
    CheckAgrees(level, 12.0, 7.0, 4.0, true, "world yaw, level parent, combined pose");
    CheckAgrees(pitched, 12.0, 7.0, 4.0, true, "world yaw, pitched parent, combined pose");
    CheckAgrees(pitched, 12.0, 7.0, 4.0, false, "local yaw, pitched parent, combined pose");
    CheckAgrees(pitched, -20.0, -14.0, -9.0, false, "local yaw, pitched parent, negative pose");
}

// In camera-local mode the parent drops out entirely: the relative rotation is
// the head rotation itself, which is what makes this the cheap mode to reason
// about and the one the equality above can be read off by eye.
void LocalModeRelativeIsTheHeadRotationAlone() {
    const ue::FRotator parent{-32.0, 145.0, 11.0};
    const ue::FRotator rel = BeamRelative(parent, 10.0, 6.0, 4.0, /*worldSpaceYaw=*/false, kLead);
    const ue::FRotator head = ue::QuatToRotator(
        ue::QuatFromEulerDeg(6.0 * kLead, 10.0 * kLead, -4.0 * kLead));
    CheckNear(rel.Pitch, head.Pitch, "local yaw: relative pitch is the head's");
    CheckNear(rel.Yaw, head.Yaw, "local yaw: relative yaw is the head's");
    CheckNear(rel.Roll, head.Roll, "local yaw: relative roll is the head's, negated");
}

void MultiplierScalesEveryAxis() {
    const ue::FRotator parent{0.0, 0.0, 0.0};
    const ue::FRotator rel = BeamRelative(parent, 10.0, 8.0, 6.0, /*worldSpaceYaw=*/true, kLead);
    CheckNear(rel.Yaw, 15.0, "1.5x scales yaw");
    CheckNear(rel.Pitch, 12.0, "1.5x scales pitch");
    CheckNear(rel.Roll, -9.0, "1.5x scales roll, negated at the boundary");
}

// 0 is a real setting, not a disabled feature: it pins the beam to the aim,
// which is where the game puts it unmodded.
void ZeroMultiplierPinsTheBeamToTheAim() {
    const ue::FRotator parent{-32.0, 145.0, 11.0};
    for (const bool worldSpaceYaw : {true, false}) {
        const ue::FRotator rel = BeamRelative(parent, 25.0, 15.0, 10.0, worldSpaceYaw, 0.0f);
        CheckNear(rel.Pitch, 0.0, "zero multiplier leaves pitch on the aim");
        CheckNear(rel.Yaw, 0.0, "zero multiplier leaves yaw on the aim");
        CheckNear(rel.Roll, 0.0, "zero multiplier leaves roll on the aim");
    }
}

void ZeroPoseLeavesTheBeamOnTheAim() {
    const ue::FRotator parent{-32.0, 145.0, 11.0};
    for (const bool worldSpaceYaw : {true, false}) {
        const ue::FRotator rel = BeamRelative(parent, 0.0, 0.0, 0.0, worldSpaceYaw, kLead);
        CheckNear(rel.Pitch, 0.0, "zero pose leaves pitch on the aim");
        CheckNear(rel.Yaw, 0.0, "zero pose leaves yaw on the aim");
        CheckNear(rel.Roll, 0.0, "zero pose leaves roll on the aim");
    }
}

// The lead is the whole point of the feature: the beam has to turn further than
// the view, not the same amount.
void TheBeamLeadsTheView() {
    const ue::FRotator parent{0.0, 0.0, 0.0};
    const double beam = BeamRelative(parent, 20.0, 0.0, 0.0, true, kLead).Yaw;
    const double view = BeamRelative(parent, 20.0, 0.0, 0.0, true, 1.0f).Yaw;
    CheckNear(view, 20.0, "1.0x matches the view");
    CheckNear(beam, 30.0, "the default leads it");
    if (beam > view) return;
    std::printf("FAIL: the beam does not lead the view\n");
    ++g_failures;
}

}  // namespace

int main() {
    BeamLandsWhereTheCompositionAsked();
    LocalModeRelativeIsTheHeadRotationAlone();
    MultiplierScalesEveryAxis();
    ZeroMultiplierPinsTheBeamToTheAim();
    ZeroPoseLeavesTheBeamOnTheAim();
    TheBeamLeadsTheView();

    if (g_failures != 0) {
        std::printf("%d check(s) failed\n", g_failures);
        return EXIT_FAILURE;
    }
    std::printf("all checks passed\n");
    return EXIT_SUCCESS;
}
