// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "build_profile.h"

// Every Xbox / Game Pass (GDK) build of Trepang2 this mod knows about.
// Append-only: a patch gets a new kGdkProfile_<date> at the top of
// kKnownProfiles in build_registry.cpp, and the profiles below stay for players
// who have not updated.
//
// The GDK exe cannot be read off disk (its ACL grants read only to the package
// identity), so these numbers were read from the module image of the running
// process, anchored on the same code the Steam profile's were.

namespace t2_ht::builds
{
    // CPPFPS-WinGDK-Shipping.exe, package Team17DigitalLimited.Trepang2 1.0.15.0,
    // Unreal Engine 4.27.
    extern const BuildProfile kGdkProfile_20260318 = {
        "gdk-wingdk-20260318",
        { 0x69BA8ABEu, 0x05401000u, 0x0515AA7Du },
        {
            // APlayerController::GetPlayerViewPoint: the function that formats
            // "APlayerController::GetPlayerViewPoint: out_Location, ViewTarget=%s".
            // Same body as the Steam build: PlayerCameraManager at +0x2b8,
            // CameraCache.Timestamp at +0x1ae0, GetCameraViewPoint at vtable +0x708.
            0x02d267a0ULL,

            // Slot 1 is ULocalPlayer::GetViewPoint, the same call site shape as
            // the Steam build: PlayerCameraManager->GetFOVAngle() (vtable +0x6d0)
            // stored at OutViewInfo+0x18, then GetPlayerViewPoint (vtable +0x718)
            // with +0x00 / +0x0c as the out-params. The other callers carry no
            // pose and are not listed.
            {{
                0x02b80b9aULL,  // 1: ULocalPlayer::GetViewPoint - render path
                0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL,
                0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL,
            }},
            inject::kFirstCaller,

            // FMinimalViewInfo: FOV 0x18, Rotation 0x0c, AspectRatio 0x2c.
            { 0x18, 0x0c, 0x2c },

            {
                // UObject::ProcessEvent compares InternalIndex against
                // NumElements at 0x04e01724; the chunk table sits 0x14 below it.
                0x04e01710ULL,
                0x14,
                0x18,
                0x10000,
                // The pool the FNamePool constructor (0x012d26a0) is handed.
                0x04dc53c0ULL,
                0x10,
                0x10,
                0x18,
                0x20,
            },

            // Same prologue and InternalIndex test as the Steam build's.
            0x014cb8a0ULL,

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
