// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "cameraunlock/input/key_bindings.h"

namespace t2_ht::hotkey_overlap {

// Whether one key press can fire both bindings, under the rule core's
// RegisterKeyBindings applies: a binding with modifiers fires while every one it
// names is held, and a binding without fires unless Ctrl and Shift are both
// held. So End and Ctrl+End clash (Ctrl+End fires both), End and Ctrl+Shift+End
// do not, and two bindings with modifiers on one key always do, since holding
// both sets fires both. The poller runs every action registered on a key, so a
// clash between two actions' lists is two actions on one press.
inline bool FireTogether(const cameraunlock::input::KeyBinding& a, const cameraunlock::input::KeyBinding& b) {
    using cameraunlock::input::HasModifiers;
    using cameraunlock::input::KeyModifiers;
    if (a.vk != b.vk) return false;
    constexpr KeyModifiers kChord = KeyModifiers::kCtrl | KeyModifiers::kShift;
    if (a.modifiers == KeyModifiers::kNone) return !HasModifiers(b.modifiers, kChord);
    if (b.modifiers == KeyModifiers::kNone) return !HasModifiers(a.modifiers, kChord);
    return true;
}

}  // namespace t2_ht::hotkey_overlap
