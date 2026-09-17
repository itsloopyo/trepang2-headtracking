// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cmath>
#include <cstdint>

// The field of view the frame is actually drawn with, and how it spreads over
// the two screen axes.
//
// The aim mark divides by the half-field tangents, so a wrong field of view puts
// it a proportional distance from where the rounds go, and the zoom compensation
// needs the live value against the game's un-zoomed one.
//
// The engine side lives in camera_fov.cpp. Everything above that line here is
// pure and header-only, so the tangent maths runs in tests with no game.
namespace t2_ht::camera_fov {

// What counts as a believable field of view, in degrees. Wide enough for every
// realistic FOV, so a value outside it is struct drift after a game patch or an
// uninitialised frame rather than a setting.
constexpr float kMinDegrees = 10.0f;
constexpr float kMaxDegrees = 170.0f;

// The aspect the FOV scalar is taken to be quoted at when the engine holds the
// field of view on the vertical axis - FMinimalViewInfo::AspectRatio, which the
// mod reads off the live view info rather than assuming. Read on every frame
// whatever the constraint says: HalfFieldTangents range-checks it, and until the
// constraint resolves it also decides whether the two models agree closely
// enough to project at all. The VerticalFixed branch below then consumes its
// value, and that branch is unverified; see its comment. 1.777778 is the engine's
// own default for it, and is what ReferenceAspect() answers until a frame has
// been read.
constexpr float kReferenceAspect = 16.0f / 9.0f;

// What that field can believably hold. Outside this it is not an aspect ratio,
// so the read is rejected and the default stands.
constexpr float kMinReferenceAspect = 0.5f;
constexpr float kMaxReferenceAspect = 5.0f;

// How the engine spreads one FOV scalar over the two axes. UE picks between
// these from ULocalPlayer::AspectRatioAxisConstraint and the viewport shape;
// they agree exactly at the reference aspect and diverge hard away from it,
// which is why the mod reads the constraint rather than assuming one. On a
// 32:9 display the wrong choice moves the reticle about twice as far
// horizontally as it should.
enum class AspectModel {
    // MaintainXFOV, and MajorAxisFOV on a viewport wider than it is tall. The
    // scalar IS the horizontal field; the vertical narrows as the display gets
    // wider (Vert-).
    HorizontalFixed,
    // MaintainYFOV. The scalar is taken as the horizontal field at
    // kReferenceAspect, with the vertical derived from it held put and the
    // horizontal growing with the display (Hor+).
    //
    // UNVERIFIED, and the only branch here that is. Both shipped profiles
    // measure ULocalPlayer::AspectRatioAxisConstraint as 1 (MaintainXFOV), so
    // every frame in every tested session takes HorizontalFixed and this
    // formula has never been exercised against a running game. It is a reading
    // of UE's projection-multiplier branch, not a measurement, and the two
    // models diverge by a factor of the viewport aspect away from 16:9 - so if
    // a patch or another title ever selects MaintainYFOV, check this against
    // FMinimalViewInfo::CalculateProjectionMatrixGivenView before trusting a
    // mark drawn through it.
    VerticalFixed,
};

// EAspectRatioAxisConstraint, in the order the shipping exe's own UEnum name
// table lists it. -1 is this mod's "not read yet", not an engine value.
inline constexpr int kConstraintUnknown = -1;
inline constexpr int kMaintainYFOV = 0;
inline constexpr int kMaintainXFOV = 1;
inline constexpr int kMajorAxisFOV = 2;

// UE's own rule, from the branch that picks the projection matrix multipliers:
// the horizontal field is the fixed one when the constraint says so outright,
// or when it says "major axis" and the viewport's major axis is the horizontal.
inline AspectModel ModelFor(int constraint, float viewportAspect) {
    if (constraint == kMaintainXFOV) return AspectModel::HorizontalFixed;
    if (constraint == kMajorAxisFOV && viewportAspect > 1.0f)
        return AspectModel::HorizontalFixed;
    return AspectModel::VerticalFixed;
}

// Close enough to the reference aspect that the two models put the reticle in
// the same place to within a couple of percent of the frame, so which one the
// engine uses stops mattering. A 16:9 display sits inside; an ultrawide does
// not, and there the constraint has to be read for real.
inline bool ModelsAgreeAt(float viewportAspect, float referenceAspect) {
    const float ratio = viewportAspect / referenceAspect;
    return ratio > 0.98f && ratio < 1.02f;
}

// Phrased as a range test rather than its negation so a NaN, which fails every
// comparison, is rejected instead of passed through.
inline bool Plausible(float degrees) {
    return degrees >= kMinDegrees && degrees <= kMaxDegrees;
}

constexpr float kDegreesToRadians = 0.01745329252f;

// tan of half an angle given in degrees: the half-field tangent of one axis,
// and the term the zoom factor is built from.
inline float TanHalf(float degrees) {
    return std::tan(degrees * 0.5f * kDegreesToRadians);
}

// Half-field tangents for the frame: tan(fovX/2) and tan(fovY/2).
//
// False - project nothing - when the field of view is not usable, or when the
// engine's aspect constraint has not been read AND the viewport is far enough
// from the reference aspect for the two models to disagree. Guessing there is
// what draws a mark where the rounds are not going, and no mark beats a wrong
// one. Inside the agreement band the models are interchangeable, so an
// unresolved constraint costs a 16:9 player nothing.
inline bool HalfFieldTangents(int constraint, float fovDegrees,
                              float viewportAspect, float referenceAspect,
                              float& tanX, float& tanY) {
    if (!Plausible(fovDegrees) || !(viewportAspect > 0.0f)) return false;
    if (!(referenceAspect >= kMinReferenceAspect &&
          referenceAspect <= kMaxReferenceAspect)) return false;
    if (constraint == kConstraintUnknown &&
        !ModelsAgreeAt(viewportAspect, referenceAspect)) return false;

    const float t = TanHalf(fovDegrees);
    if (!(t > 0.0f)) return false;
    if (ModelFor(constraint, viewportAspect) == AspectModel::HorizontalFixed) {
        tanX = t;
        tanY = t / viewportAspect;
    } else {
        tanY = t / referenceAspect;
        tanX = tanY * viewportAspect;
    }
    return tanX > 0.0f && tanY > 0.0f;
}

// ---- the engine side -----------------------------------------------------

// The FOV the render caller's FMinimalViewInfo carries, or 0 when the two
// out-params are not fields of one view info or the value is not an angle.
float ReadRenderFov(const void* outLocation, const void* outRotation);

// ULocalPlayer::AspectRatioAxisConstraint, re-read every call once its offset
// is resolved: the game can change it when its own FOV setting is applied.
void RefreshAspectConstraint(std::uintptr_t controller);
int AspectConstraint();

// FMinimalViewInfo::AspectRatio as last read. kReferenceAspect until then.
float ReferenceAspect();

// The game's own un-zoomed field of view: the FOV slider in the video settings
// (GameInstanceBP_C.SettingsSaveObj.VideoSettings.FOV_*), in degrees, or 0 when
// it does not read. This is the reference the zoom compensation scales against.
float BaseFov();

}  // namespace t2_ht::camera_fov
