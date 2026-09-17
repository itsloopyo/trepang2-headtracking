// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstdint>

#include <cameraunlock/unreal/ue_math.h>

// What the player's own character is doing this frame, read off the live
// object graph through the engine's reflection data.
//
// Trepang2's player shots take their origin and direction from
// BaseWeapon::GetShootLocation / GetShootAngles, which on the player character
// resolve to AActor::GetActorEyesViewPoint and so to the controller's
// GetPlayerViewPoint through a caller the view hook never writes to. The aim
// ray is therefore the clean view the render caller is handed, and nothing here
// has to reconstruct it from the weapon.
namespace t2_ht::player_rig {

struct Transform {
    bool Valid = false;
    cameraunlock::unreal::FVector Position{0.0, 0.0, 0.0};
    cameraunlock::unreal::FVector Forward{1.0, 0.0, 0.0};
    cameraunlock::unreal::FQuat4d Rotation{0.0, 0.0, 0.0, 1.0};
};

struct Snapshot {
    // The BasePlayer the controller possesses, or 0 for any other pawn (or none).
    std::uintptr_t Pawn = 0;
    std::uintptr_t Weapon = 0;
    // BasePlayer::FirstPersonCameraComponent. Gameplay is drawn from it; a
    // cutscene or a death camera is drawn from somewhere else. The torch hangs
    // off this component, so its pointer is what identifies the beam's parent.
    std::uintptr_t Camera = 0;
    Transform FirstPersonCamera;
    bool Dead = false;
    // BasePlayer::ZoomAlpha above zero, or bWantZoom: the sights are coming up,
    // are up, or are still on their way down.
    bool AimingDownSights = false;
};

// Game thread only. `controller` is the player controller the view hook holds.
Snapshot Read(std::uintptr_t controller);

}  // namespace t2_ht::player_rig
