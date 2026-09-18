// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "build_profile.h"

// Every GOG build of Trepang2 this mod knows about. Append-only: a patch gets a
// new kGogProfile_<date> at the top of kKnownProfiles in build_registry.cpp, and
// the profiles below stay for players who have not updated.

namespace t2_ht::builds
{
    // CPPFPS-Win64-Shipping.exe, GOG product 1599916752 (GOG version 82.00),
    // Unreal Engine 4.27. Same exe name and folder as the Steam build, but a
    // different binary with its own RVAs.
    extern const BuildProfile kGogProfile_20240805 = {
        "gog-win64-20240805",
        { 0x66B15AD7u, 0x0559B000u, 0x052E6407u },
        {
            // APlayerController::GetPlayerViewPoint. Same body as the Steam
            // build: PlayerCameraManager at +0x2b8, CameraCache.Timestamp at
            // +0x1ae0, GetCameraViewPoint at vtable +0x708.
            0x02f4dba0ULL,

            // Slot 1 is ULocalPlayer::GetViewPoint (0x02da7ba0), the same call
            // site shape as the other builds: PlayerCameraManager->GetFOVAngle()
            // (vtable +0x6d0) stored at OutViewInfo+0x18, then
            // GetPlayerViewPoint (vtable +0x718) with +0x00 / +0x0c as the
            // out-params. The other callers carry no pose and are not listed.
            {{
                0x02da7e5aULL,  // 1: ULocalPlayer::GetViewPoint - render path
                0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL,
                0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL,
            }},
            inject::kFirstCaller,

            // FMinimalViewInfo: FOV 0x18, Rotation 0x0c, AspectRatio 0x2c.
            { 0x18, 0x0c, 0x2c },

            {
                // GUObjectArray is at 0x04f3a460, its ObjObjects member 0x10 in.
                // UObject::ProcessEvent compares InternalIndex against
                // NumElements at 0x04f3a484; the chunk table sits 0x14 below it.
                0x04f3a470ULL,
                0x14,
                0x18,
                0x10000,
                // The pool the FNamePool constructor (0x01388630) is handed.
                0x04efe100ULL,
                0x10,
                0x10,
                0x18,
                0x20,
            },

            // Same prologue and InternalIndex test as the Steam build's.
            0x01582e50ULL,

            {
                0x08,   // kFField_ClassPrivate
                0x20,   // kFField_Next
                0x28,   // kFField_NamePrivate
                0x38,   // kFProperty_ArrayDim
                0x3c,   // kFProperty_ElementSize
                0x4c,   // kFProperty_Offset
                0x00,   // kFFieldClass_Name
                0x40,   // kUStruct_SuperStruct
                0x50,   // kUStruct_ChildProperties
                0x58,   // kUStruct_PropertiesSize
                0x78,   // kFBoolProperty_FieldSize
                0x78,   // kFStructProperty_Struct
            },
        },
    };
}
