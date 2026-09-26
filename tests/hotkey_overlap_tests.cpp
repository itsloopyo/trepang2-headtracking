// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Which pairs of bindings one key press fires together, so the mod can keep a
// key a player or Defaults.ini put in two actions' lists to the first action.

#include "hotkey_overlap.h"
#include "test_harness.h"

namespace {

using cameraunlock::input::KeyBinding;
using cameraunlock::input::KeyModifiers;
using t2_ht::hotkey_overlap::FireTogether;

constexpr int kVkEnd = 0x23;
constexpr int kVkPageUp = 0x21;
constexpr KeyModifiers kNone = KeyModifiers::kNone;
constexpr KeyModifiers kCtrl = KeyModifiers::kCtrl;
constexpr KeyModifiers kShift = KeyModifiers::kShift;
constexpr KeyModifiers kAlt = KeyModifiers::kAlt;
constexpr KeyModifiers kChord = kCtrl | kShift;

bool Both(KeyBinding a, KeyBinding b) {
    const bool ab = FireTogether(a, b);
    CHECK_MSG(ab == FireTogether(b, a), "the answer does not depend on which list came first");
    return ab;
}

void TestDifferentKeysNeverClash() {
    CHECK(!Both({kNone, kVkEnd}, {kNone, kVkPageUp}));
    CHECK(!Both({kChord, kVkEnd}, {kChord, kVkPageUp}));
}

void TestTheSameBareKeyClashes() {
    CHECK(Both({kNone, kVkEnd}, {kNone, kVkEnd}));
}

// Ctrl+End holds Ctrl but not Shift, so the bare End fires with it.
void TestABareKeyClashesWithItsOneModifierForms() {
    CHECK(Both({kNone, kVkEnd}, {kCtrl, kVkEnd}));
    CHECK(Both({kNone, kVkEnd}, {kShift, kVkEnd}));
    CHECK(Both({kNone, kVkEnd}, {kAlt, kVkEnd}));
    CHECK(Both({kNone, kVkEnd}, {kCtrl | kAlt, kVkEnd}));
}

// A bare key is silent while Ctrl and Shift are both held.
void TestABareKeyLeavesItsCtrlShiftFormsAlone() {
    CHECK(!Both({kNone, kVkEnd}, {kChord, kVkEnd}));
    CHECK(!Both({kNone, kVkEnd}, {kChord | kAlt, kVkEnd}));
}

// Holding both sets of modifiers fires both.
void TestTwoModifierFormsOfOneKeyClash() {
    CHECK(Both({kCtrl, kVkEnd}, {kShift, kVkEnd}));
    CHECK(Both({kChord, kVkEnd}, {kAlt, kVkEnd}));
    CHECK(Both({kChord, kVkEnd}, {kChord, kVkEnd}));
}

}  // namespace

int main() {
    TestDifferentKeysNeverClash();
    TestTheSameBareKeyClashes();
    TestABareKeyClashesWithItsOneModifierForms();
    TestABareKeyLeavesItsCtrlShiftFormsAlone();
    TestTwoModifierFormsOfOneKeyClash();

    return t2_test::Report();
}
