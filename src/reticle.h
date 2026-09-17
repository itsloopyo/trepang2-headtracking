// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstdint>

// The game's own crosshair, moved onto the point the shot will land.
//
// Trepang2 paints everything that marks the aim point at the middle of the
// screen: the crosshair, the stealth dot, the kill confirmation, the tactical
// visor's crosshair, the prompts for the weapon, grenade or interaction under
// it, the ability keys and the out-of-ammo text. That is right only while the
// drawn view is the aim. With the head turned or leaned it is not, so the mod
// moves the HUD panels holding them by the screen offset of the projected aim
// point, through the engine's own UWidget::SetRenderTranslation. The marks on
// screen are the game's, and no second one is drawn.
namespace t2_ht::reticle {

// Game thread. `controller` is the player controller, `pawn` the player
// character that owns the HUD. `valid` false puts the crosshair back where the
// game laid it out, which is what every frame without a usable projection
// does. `ndcX` / `ndcY` are -1..1, x right and y up.
void Publish(std::uintptr_t controller, std::uintptr_t pawn, bool valid, float ndcX, float ndcY);

// The aim marker for the `marker` ADS mode. Raising the sights hides the game's
// crosshair (the sight picture is the aim), so while head tracking carries on
// through an aim the mod puts a second instance of the game's own crosshair
// widget in the viewport at the projected impact point. Called every render
// frame, with `visible` false whenever the marker should not be on screen.
// Returns whether the marker is on screen after the call.
bool PublishMarker(std::uintptr_t controller, bool visible, float ndcX, float ndcY);

}  // namespace t2_ht::reticle
