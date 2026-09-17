// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// What HeadTracking.ini is allowed to put into the camera.
//
// The INI is the one place a player's text becomes a float the render hook
// multiplies a pose by, so it is a system boundary and every hazard belongs
// here rather than downstream. Three of them are reachable from a single typo
// and none of them used to be caught:
//
//   - "nan" parses. A range test phrased as a rejection lets it through
//     (a NaN fails both comparisons), and a NaN smoothing value or collision
//     margin reaches the camera as a NaN rotation written every frame with
//     nothing in the log.
//   - "0,15" - a European decimal comma - parses as a PREFIX. It yields 0.0,
//     which is inside every valid range, so the user's setting is silently
//     replaced by one they did not choose.
//   - "1e400" overflows to +inf.
//
// The suite writes real INI files into a temp directory and reads them back
// through config::Load, because the reader underneath is
// GetPrivateProfileStringA and its behaviour is the thing being pinned.

#include <cstdio>
#include <string>

#include <windows.h>

#include "config.h"
#include "test_harness.h"

namespace {

using t2_ht::Config;

std::string TempDirectory() {
    char base[MAX_PATH] = {};
    const DWORD n = GetTempPathA(MAX_PATH, base);
    std::string dir(base, n);
    dir += "t2_config_tests";
    CreateDirectoryA(dir.c_str(), nullptr);
    return dir;
}

const std::string& Directory() {
    static const std::string dir = TempDirectory();
    return dir;
}

std::string IniPath() { return Directory() + "\\HeadTracking.ini"; }

// Fresh file every time: GetPrivateProfile* caches by path plus write time, so
// a rewritten file has to be written through the same API surface the reader
// uses rather than left to a second-granularity timestamp.
void WriteIni(const std::string& body) {
    DeleteFileA(IniPath().c_str());
    FILE* f = std::fopen(IniPath().c_str(), "wb");
    if (!f) return;
    std::fwrite(body.data(), 1, body.size(), f);
    std::fclose(f);
    WritePrivateProfileStringA(nullptr, nullptr, nullptr, IniPath().c_str());
}

Config Load(const std::string& body) {
    WriteIni(body);
    Config out;
    t2_ht::config::Load(Directory(), out);
    return out;
}

void TestAWellFormedValueIsTaken() {
    const Config c = Load(
        "[Tracking]\r\nLocalSmoothing=0.25\r\nRemoteSmoothing=0.40\r\n"
        "[Camera]\r\nCollisionMargin=12.5\r\n");
    CHECK_NEAR(c.local_smoothing, 0.25, 1e-6);
    CHECK_NEAR(c.remote_smoothing, 0.40, 1e-6);
    CHECK_NEAR(c.collision_margin, 12.5, 1e-6);
}

void TestNotANumberIsRejected() {
    const Config c = Load("[Tracking]\r\nLocalSmoothing=nan\r\nRemoteSmoothing=-nan(ind)\r\n"
                          "[Camera]\r\nCollisionMargin=nan\r\n");
    CHECK_MSG(c.local_smoothing == 0.0f, "a NaN LocalSmoothing must leave the default in force");
    CHECK_MSG(c.remote_smoothing == 0.15f, "a NaN RemoteSmoothing must leave the default in force");
    CHECK_MSG(c.collision_margin == 10.0f, "a NaN CollisionMargin must leave the default in force");
}

void TestInfinityIsRejected() {
    const Config c = Load("[Tracking]\r\nLocalSmoothing=inf\r\nRemoteSmoothing=1e400\r\n");
    CHECK(c.local_smoothing == 0.0f);
    CHECK(c.remote_smoothing == 0.15f);
}

// The typo this catches used to pass every check: strtod stops at the comma,
// yields 0.0, and 0.0 is a legal smoothing value.
void TestADecimalCommaIsRejectedRatherThanTruncated() {
    const Config c = Load("[Tracking]\r\nRemoteSmoothing=0,15\r\n");
    CHECK_MSG(c.remote_smoothing == 0.15f,
              "a European decimal comma must not be read as its integer part");
}

void TestTrailingTextIsRejected() {
    const Config c = Load("[Camera]\r\nCollisionMargin=12.5cm\r\n");
    CHECK(c.collision_margin == 10.0f);
}

// A comment after a numeric value is the one form that has to keep working:
// GetPrivateProfileStringA hands the comment back as part of the value, and the
// shipped default INI puts comments on their own lines only because of it.
void TestATrailingCommentStillParses() {
    const Config c = Load("[Camera]\r\nCollisionMargin=20.0 ; held off the wall\r\n");
    CHECK_NEAR(c.collision_margin, 20.0, 1e-6);
}

// A bool went through IniReader::ReadBool, which compares the whole value and
// so read `0 ; comment` as no match at all - the user's edit discarded in the
// direction that leaves the clamp on.
void TestABoolWithATrailingCommentIsTaken() {
    const Config c = Load("[Camera]\r\nCollisionEnabled=0 ; walls are fine\r\n");
    CHECK_MSG(c.collision_enabled == false,
              "a bool with a trailing comment must be read, not silently defaulted");
    const Config on = Load("[Dev]\r\nDevCommands=1 # switched on for a test\r\n");
    CHECK_MSG(on.dev_commands == true,
              "a '#' comment must not hide a bool whose default is false");
}

void TestAnUnparseableBoolKeepsItsDefault() {
    const Config d;
    const Config c = Load("[Camera]\r\nCollisionEnabled=maybe\r\n");
    CHECK(c.collision_enabled == d.collision_enabled);
}

void TestAnOutOfRangeValueFallsBackToTheDefault() {
    const Config c = Load("[Tracking]\r\nLocalSmoothing=2.0\r\n[Camera]\r\nCollisionMargin=400\r\n");
    CHECK(c.local_smoothing == 0.0f);
    CHECK(c.collision_margin == 10.0f);
}

void TestAnAbsentKeyKeepsItsDefault() {
    const Config c = Load("[Network]\r\nPort=5771\r\n");
    CHECK(c.udp_port == 5771);
    CHECK(c.local_smoothing == 0.0f);
    CHECK(c.remote_smoothing == 0.15f);
    CHECK(c.collision_margin == 10.0f);
    CHECK(c.collision_release_smoothing == 0.9f);
}

void TestAnEmptyValueKeepsItsDefault() {
    const Config c = Load("[Tracking]\r\nLocalSmoothing=\r\nRemoteSmoothing= ; nothing here\r\n");
    CHECK(c.local_smoothing == 0.0f);
    CHECK(c.remote_smoothing == 0.15f);
}

void TestAPortOutsideTheRangeFallsBack() {
    CHECK(Load("[Network]\r\nPort=70000\r\n").udp_port == 4242);
    CHECK(Load("[Network]\r\nPort=80\r\n").udp_port == 4242);
    CHECK(Load("[Network]\r\nPort=notaport\r\n").udp_port == 4242);
}

void TestAnUnbindableHotkeyFallsBack() {
    const Config c = Load("[Hotkeys]\r\nYawMode=0x230\r\nAdsMode=0x10\r\n");
    CHECK_MSG(c.yaw_mode_key == 0x22, "a key code the poller cannot watch must not be bound");
    CHECK_MSG(c.ads_mode_key == 0x2D, "a modifier must not be bound");
}

// The lead is the one setting whose out-of-range value has a tempting wrong
// answer: clamping 8 to 5 leaves the file saying 8 and the beam running at 5.
void TestTheTorchLeadIsTakenAndAnOutOfRangeOneIsRefused() {
    const Config taken = Load("[Light]\r\nLightFollowsHead=0\r\nLightMultiplier=2.25\r\n");
    CHECK(taken.light_follows_head == false);
    CHECK_NEAR(taken.light_multiplier, 2.25, 1e-6);

    const Config refused = Load("[Light]\r\nLightMultiplier=8\r\n");
    CHECK_MSG(refused.light_multiplier == cameraunlock::effects::kDefaultLightMultiplier,
              "a multiplier past the bound must leave the default in force, not be clamped");
}

void TestTheDefaultFileIsNotOverwritten() {
    WriteIni("[Tracking]\r\nLocalSmoothing=0.5\r\n");
    t2_ht::config::WriteDefaultIfMissing(Directory());
    Config out;
    t2_ht::config::Load(Directory(), out);
    CHECK_MSG(out.local_smoothing == 0.5f,
              "WriteDefaultIfMissing must leave an existing INI alone");
}

void TestTheWrittenDefaultReadsBackAsTheDefaults() {
    DeleteFileA(IniPath().c_str());
    t2_ht::config::WriteDefaultIfMissing(Directory());
    Config out;
    t2_ht::config::Load(Directory(), out);
    const Config d;
    CHECK(out.udp_port == d.udp_port);
    CHECK(out.local_smoothing == d.local_smoothing);
    CHECK(out.remote_smoothing == d.remote_smoothing);
    CHECK(out.collision_margin == d.collision_margin);
    CHECK(out.collision_enabled == d.collision_enabled);
    CHECK(out.world_space_yaw == d.world_space_yaw);
    CHECK(out.yaw_mode_key == d.yaw_mode_key);
    CHECK(out.ads_mode_key == d.ads_mode_key);
    CHECK(out.ads_mode == d.ads_mode);
    CHECK(out.light_follows_head == d.light_follows_head);
    CHECK(out.light_multiplier == d.light_multiplier);
}

}  // namespace

int main() {
    TestAWellFormedValueIsTaken();
    TestNotANumberIsRejected();
    TestInfinityIsRejected();
    TestADecimalCommaIsRejectedRatherThanTruncated();
    TestTrailingTextIsRejected();
    TestATrailingCommentStillParses();
    TestABoolWithATrailingCommentIsTaken();
    TestAnUnparseableBoolKeepsItsDefault();
    TestAnOutOfRangeValueFallsBackToTheDefault();
    TestAnAbsentKeyKeepsItsDefault();
    TestAnEmptyValueKeepsItsDefault();
    TestAPortOutsideTheRangeFallsBack();
    TestAnUnbindableHotkeyFallsBack();
    TestTheTorchLeadIsTakenAndAnOutOfRangeOneIsRefused();
    TestTheDefaultFileIsNotOverwritten();
    TestTheWrittenDefaultReadsBackAsTheDefaults();

    DeleteFileA(IniPath().c_str());
    return t2_test::Report();
}
