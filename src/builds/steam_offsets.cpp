// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "build_profile.h"

// Every Steam build of Trepang2 this mod knows about. Append-only: a patch
// gets a new kSteamProfile_<date> at the top of kKnownProfiles in
// build_registry.cpp, and the profiles below stay for players who have not
// updated.

namespace t2_ht::builds
{
    // CPPFPS-Win64-Shipping.exe, Unreal Engine 4.27.
    extern const BuildProfile kSteamProfile_20240730 = {
        "steam-win64-20240730",
        { 0x66A96DCBu, 0x055CD000u, 0x052E140Fu },
        {
            // APlayerController::GetPlayerViewPoint: the function that formats
            // "APlayerController::GetPlayerViewPoint: out_Location, ViewTarget=%s".
            // Reads PlayerCameraManager at +0x2b8 and CameraCache.Timestamp at
            // +0x1ae0 of it before dispatching GetCameraViewPoint (vtable +0x708).
            0x02f4ed50ULL,

            // Captured in game with inject mode 0 (title screen, safehouse).
            // Slot 1 is ULocalPlayer::GetViewPoint: its call site stores
            // PlayerCameraManager->GetFOVAngle() (vtable +0x6d0) at
            // OutViewInfo+0x18 and passes +0x00 / +0x0c as the out-params. It
            // is the only caller whose rotation sits 0x0c after its location.
            {{
                0x02da900aULL,  // 1: ULocalPlayer::GetViewPoint - render path
                0x02e55fd2ULL,
                0x02d9234dULL,
                0x00c8e673ULL,
                0x02cc4916ULL,
                0x02f4a344ULL,
                0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL,
            }},
            inject::kFirstCaller,

            // FMinimalViewInfo property table: Location 0x00, Rotation 0x0c,
            // FOV 0x18, DesiredFOV 0x1c, AspectRatio 0x2c.
            { 0x18, 0x0c, 0x2c },

            {
                // UObject::ProcessEvent compares InternalIndex against
                // NumElements at 0x04f3b484; the chunk table sits 0x14 below it,
                // indexed (idx >> 16) * 8 then idx * 0x18.
                0x04f3b470ULL,
                0x14,
                0x18,
                0x10000,
                // The pool the FNamePool constructor (0x01389810) is handed.
                0x04eff100ULL,
                0x10,
                0x10,
                0x18,
                0x20,
            },

            // Opens by comparing this->InternalIndex (+0x0c) with the object
            // array's element count before resolving the object item.
            0x01584030ULL,

            // UE 4.27 FField / FProperty layout: Owner at +0x10 is a 16-byte
            // FFieldVariant, so Next sits at +0x20 and Offset_Internal at +0x4c.
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
