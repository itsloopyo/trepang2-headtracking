// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "config.h"
#include "session.h"

// The mod's key bindings: the key lists CameraUnlock.ini holds for each action,
// the Ctrl+Shift chords among them. Every binding does its work through
// view_hook or the session and says what it did in the log, so this is the only
// place that knows which key means what.
namespace t2_ht::hotkeys {

// Register the bindings and start polling. `session` must outlive the poller.
void Register(const Config& config, Session& session);

// Stop the polling thread. Safe to call when nothing was registered.
void Stop();

}  // namespace t2_ht::hotkeys
