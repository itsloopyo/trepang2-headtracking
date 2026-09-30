// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// CameraUnlock.ini in the canonical config format.
//
// The committed CameraUnlock.ini is the table's fresh render, which is also what
// the owner creates beside the game exe at first launch: `default` on every
// global row, so each follows Defaults.ini, and the game's own value on the rows
// it keeps. A toggle's save changes the lines of its rows and no other byte. An
// older HeadTracking.ini is imported once into a new CameraUnlock.ini through the
// frozen import and is never written; tests/config_differential/ holds that to
// the published build over the whole corpus, and the cases here are the ones
// worth reading as examples.
//
// `t2_config_tests --render-config <path>` writes the fresh render to <path> and
// exits, which is how `pixi run render-config` rewrites the committed file after
// a change to a row, a comment or a default.

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include <windows.h>

#include "config.h"
#include "test_harness.h"

namespace {

namespace cfg = ::cameraunlock::config;
namespace fs = std::filesystem;
using cameraunlock::TrackingMode;

std::string ReadFileBytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path.string());
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void WriteFileBytes(const fs::path& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!out) throw std::runtime_error("cannot write " + path.string());
}

std::string Rendered() { return cfg::RenderCanonicalFresh(t2_ht::config::Table(), t2_ht::config::Header()); }

std::string CommittedFile() { return ReadFileBytes(fs::path(T2_SOURCE_DIR) / "CameraUnlock.ini"); }

// A folder of its own per case, removed afterwards: `game` stands for the folder
// holding the game exe, and Defaults.ini sits in `global` beside it.
class Scratch {
public:
    explicit Scratch(const char* tag) {
        wchar_t temp[MAX_PATH + 1] = {};
        if (GetTempPathW(MAX_PATH + 1, temp) == 0) throw std::runtime_error("GetTempPathW failed");
        root_ = fs::path(temp) / ("t2_ht_config_" + std::string(tag) + "_" + std::to_string(GetCurrentProcessId()));
        fs::remove_all(root_);
        fs::create_directories(game());
    }
    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;
    // A scanner can still hold a file the test just wrote, and a destructor must
    // not throw, so a folder left behind is reported and the run carries on.
    ~Scratch() {
        std::error_code error;
        fs::remove_all(root_, error);
        if (error) std::printf("  scratch folder left behind: %s: %s\n", root_.string().c_str(), error.message().c_str());
    }

    fs::path game() const { return root_ / "game"; }
    fs::path ini() const { return game() / "CameraUnlock.ini"; }
    fs::path legacy() const { return game() / "HeadTracking.ini"; }
    fs::path defaults() const { return root_ / "global" / "Defaults.ini"; }

    t2_ht::Config Load() const {
        return t2_ht::config::Load(game().wstring(), cfg::DefaultsFile::At(defaults().wstring()));
    }

    std::set<std::string> Names() const {
        std::set<std::string> names;
        for (const auto& entry : fs::directory_iterator(game())) names.insert(entry.path().filename().string());
        return names;
    }

private:
    fs::path root_;
};

std::vector<std::string> Lines(const std::string& bytes) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    for (std::size_t end; (end = bytes.find("\r\n", start)) != std::string::npos; start = end + 2) {
        lines.push_back(bytes.substr(start, end - start));
    }
    return lines;
}

// The lines that differ between two files of the same line count, or "count" when
// the counts differ.
std::vector<std::string> ChangedLines(const std::string& before, const std::string& after) {
    const std::vector<std::string> a = Lines(before), b = Lines(after);
    if (a.size() != b.size()) return {"count"};
    std::vector<std::string> changed;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i] != b[i]) changed.push_back(b[i]);
    }
    return changed;
}

bool Holds(const std::string& bytes, const std::string& line) {
    return bytes.find("\r\n" + line + "\r\n") != std::string::npos;
}

void TheCommittedFileIsTheFreshRender() {
    CHECK_MSG(Rendered() == CommittedFile(), "CameraUnlock.ini is the table's fresh render; run pixi run render-config");
}

// Every global row holds `default` but the mode and yaw hotkeys, which this game
// keeps for itself (per_game). The collision margin and channel are every game's
// own, the margin as a value and the channel, an Engine row, commented at its
// default; AimTraceChannel and DevCommands are this mod's rows.
void TheCommittedFileFollowsDefaultsIni() {
    const std::string committed = CommittedFile();
    for (const char* line :
         {"UdpPort=default", "EnableOnStartup=default", "WorldSpaceYaw=default", "RotationEnabled=default",
          "PositionEnabled=default", "LocalSmoothing=default", "RemoteSmoothing=default", "CollisionEnabled=default",
          "CollisionReleaseSmoothing=default", "ToggleKey=default",
          "CycleTrackingModeKey=PageUp, Ctrl+Shift+J", "YawModeKey=PageDown", "LightMultiplier=default",
          "CollisionMargin=10.0", "; CollisionChannel=0", "; AimTraceChannel=0", "DevCommands=false"}) {
        CHECK_MSG(Holds(committed, line), line);
    }
}

void FirstLaunchCreatesTheCommittedFile() {
    Scratch s("created");
    const t2_ht::Config loaded = s.Load();
    CHECK_MSG(ReadFileBytes(s.ini()) == CommittedFile(), "the first launch writes the committed file byte for byte");
    CHECK_MSG(s.Names() == std::set<std::string>{"CameraUnlock.ini"},
              "the first launch creates CameraUnlock.ini and nothing else beside the exe");
    CHECK_MSG(fs::exists(s.defaults()), "the first launch creates Defaults.ini where none exists");
    CHECK(loaded.toggle_key == "End, Ctrl+Shift+Y");
    CHECK(loaded.cycle_tracking_mode_key == "PageUp, Ctrl+Shift+J");
    CHECK(loaded.yaw_mode_key == "PageDown");
    CHECK(loaded.enable_on_startup);
    CHECK(loaded.world_space_yaw);
    CHECK(loaded.rotation_enabled && loaded.position_enabled);
    CHECK(loaded.collision_enabled);
    CHECK(loaded.collision_margin == 10.0f);
    CHECK(loaded.light_multiplier == 1.5f);
}

// A value in Defaults.ini reaches every row holding `default`, the toggle key
// included, and not the mode and yaw keys, which this game keeps for itself.
void ADefaultRowFollowsDefaultsIni() {
    Scratch s("follows");
    s.Load();
    WriteFileBytes(s.defaults(), "[CameraUnlock]\r\nConfigFormat=1\r\n\r\n[Hotkeys]\r\nToggleKey=F8\r\n"
                                 "CycleTrackingModeKey=F9\r\nYawModeKey=F10\r\n");
    const t2_ht::Config c = s.Load();
    CHECK(c.toggle_key == "F8");
    CHECK(c.cycle_tracking_mode_key == "PageUp, Ctrl+Shift+J");
    CHECK(c.yaw_mode_key == "PageDown");
}

void TheYawToggleSavesItsLineAndNothingElse() {
    Scratch s("save_yaw");
    s.Load();
    const std::string before = ReadFileBytes(s.ini());
    const std::string defaults = ReadFileBytes(s.defaults());
    t2_ht::config::SaveWorldSpaceYaw(false);
    CHECK_MSG(ChangedLines(before, ReadFileBytes(s.ini())) == std::vector<std::string>{"WorldSpaceYaw=false"},
              "a yaw save writes WorldSpaceYaw over default, and nothing else");
    CHECK_MSG(ReadFileBytes(s.defaults()) == defaults, "a save leaves Defaults.ini as it was");
    CHECK_MSG(!s.Load().world_space_yaw, "the saved yaw mode comes back at the next launch");
}

// The mode is one setting in two rows, so a save writes both.
void TheModeCycleSavesThePair() {
    Scratch s("save_mode");
    s.Load();
    const std::string before = ReadFileBytes(s.ini());

    t2_ht::config::SaveTrackingMode(TrackingMode::RotationOnly);
    CHECK_MSG(ChangedLines(before, ReadFileBytes(s.ini())) ==
                  (std::vector<std::string>{"RotationEnabled=true", "PositionEnabled=false"}),
              "rotation only writes the pair over default");

    t2_ht::config::SaveTrackingMode(TrackingMode::PositionOnly);
    CHECK_MSG(ChangedLines(before, ReadFileBytes(s.ini())) ==
                  (std::vector<std::string>{"RotationEnabled=false", "PositionEnabled=true"}),
              "position only writes the pair");
    const t2_ht::Config reloaded = s.Load();
    CHECK_MSG(!reloaded.rotation_enabled && reloaded.position_enabled, "the saved mode comes back at the next launch");

    t2_ht::config::SaveTrackingMode(TrackingMode::RotationAndPosition);
    CHECK_MSG(ChangedLines(before, ReadFileBytes(s.ini())) ==
                  (std::vector<std::string>{"RotationEnabled=true", "PositionEnabled=true"}),
              "back to full, the pair holds values");
}

// The legacy file is imported into a new CameraUnlock.ini and left as it was.
void TheLegacyFileIsImportedAndLeftAsItWas() {
    Scratch s("import");
    const std::string legacy =
        "[General]\r\nWorldSpaceYaw=0\r\n; my note\r\n[Tracking]\r\nRemoteSmoothing=0.40\r\n"
        "[Camera]\r\nCollisionMargin=20.0\r\nCollisionChannel=2\r\n";
    WriteFileBytes(s.legacy(), legacy);
    const t2_ht::Config c = s.Load();
    CHECK(!c.world_space_yaw);
    CHECK(c.remote_smoothing == 0.4f);
    CHECK(c.collision_margin == 20.0f);
    CHECK(c.collision_channel == 2);
    CHECK_MSG(ReadFileBytes(s.legacy()) == legacy, "HeadTracking.ini keeps its bytes");
    CHECK_MSG((s.Names() == std::set<std::string>{"CameraUnlock.ini", "HeadTracking.ini"}),
              "the import creates CameraUnlock.ini and nothing else");
    const std::string migrated = ReadFileBytes(s.ini());
    CHECK(Holds(migrated, "WorldSpaceYaw=false"));
    CHECK(Holds(migrated, "RemoteSmoothing=0.4"));
    CHECK(Holds(migrated, "CollisionMargin=20.0"));
    CHECK(Holds(migrated, "CollisionChannel=2"));
    CHECK_MSG(Holds(migrated, "LocalSmoothing=default"), "a value equal to the default is written as default");

    // Once CameraUnlock.ini exists, HeadTracking.ini is not read again.
    WriteFileBytes(s.legacy(), "[General]\r\nWorldSpaceYaw=1\r\n");
    CHECK_MSG(!s.Load().world_space_yaw, "the next launch reads CameraUnlock.ini, not HeadTracking.ini");
    CHECK_MSG(ReadFileBytes(s.ini()) == migrated, "the next launch writes nothing");
}

// The yaw key was the one hotkey in the old file, and one the player changed is
// carried as it was. The toggle, bound in code before to the fleet's list,
// follows Defaults.ini; the mode cycle keeps the list it was bound to in code.
void AnOldYawKeyIsCarried() {
    Scratch s("yaw_key");
    WriteFileBytes(s.legacy(), "[View]\r\nAdsMode=tracked\r\n[Hotkeys]\r\nYawMode=0x2E\r\nAdsMode=0x2D\r\n");
    const t2_ht::Config c = s.Load();
    CHECK(c.yaw_mode_key == "Delete");
    CHECK(c.toggle_key == "End, Ctrl+Shift+Y");
    CHECK(c.cycle_tracking_mode_key == "PageUp, Ctrl+Shift+J");
    const std::string migrated = ReadFileBytes(s.ini());
    CHECK(Holds(migrated, "YawModeKey=Delete"));
    CHECK(Holds(migrated, "ToggleKey=default"));
    CHECK(Holds(migrated, "CycleTrackingModeKey=PageUp, Ctrl+Shift+J"));
    CHECK_MSG(migrated.find("AdsMode") == std::string::npos, "the retired ADS keys are not carried");
}

// The yaw key's old default, Page Down with no chord, is this game's own.
void TheOldDefaultYawKeyStaysPageDown() {
    Scratch s("yaw_default");
    WriteFileBytes(s.legacy(), "[Hotkeys]\r\nYawMode=0x22\r\n");
    const t2_ht::Config c = s.Load();
    CHECK(c.yaw_mode_key == "PageDown");
    CHECK(Holds(ReadFileBytes(s.ini()), "YawModeKey=PageDown"));
}

// The old build refused a yaw key on Ctrl alone and bound Page Down, so the import
// carries Page Down.
void AnOldYawKeyOnCtrlStaysPageDown() {
    Scratch s("yaw_ctrl");
    WriteFileBytes(s.legacy(), "[Hotkeys]\r\nYawMode=0x11\r\n");
    const t2_ht::Config c = s.Load();
    CHECK(c.yaw_mode_key == "PageDown");
    CHECK(Holds(ReadFileBytes(s.ini()), "YawModeKey=PageDown"));
}

// A setting the player left at the old build's value follows Defaults.ini, and
// one the player changed stays theirs.
void AnUntouchedSettingFollowsDefaultsIni() {
    Scratch s("untouched");
    WriteFileBytes(s.legacy(), "[Network]\r\nPort=4242\r\n[Tracking]\r\nRemoteSmoothing=0.40\r\n");
    s.Load();
    const std::string migrated = ReadFileBytes(s.ini());
    CHECK(Holds(migrated, "UdpPort=default"));
    CHECK(Holds(migrated, "RemoteSmoothing=0.4"));
    CHECK(Holds(migrated, "RotationEnabled=default"));
    CHECK(Holds(migrated, "PositionEnabled=default"));
}

// The old build refused a yaw key another action already had, and bound the yaw
// toggle to nothing; the import keeps it unbound rather than firing two actions on
// one key.
void AnOldYawKeyOnEndStaysUnbound() {
    Scratch s("yaw_end");
    WriteFileBytes(s.legacy(), "[Hotkeys]\r\nYawMode=0x23\r\n");
    const t2_ht::Config c = s.Load();
    CHECK(c.yaw_mode_key.empty());
    CHECK(Holds(ReadFileBytes(s.ini()), "YawModeKey="));
}

void TheRetiredLightSwitchIsIgnored() {
    Scratch s("retired_light");
    const std::string legacy = "[Light]\r\nLightFollowsHead=false\r\nLightMultiplier=2.25\r\n";
    WriteFileBytes(s.legacy(), legacy);
    CHECK(s.Load().light_multiplier == 2.25f);
    CHECK(ReadFileBytes(s.legacy()) == legacy);
    CHECK(ReadFileBytes(s.ini()).find("LightFollowsHead") == std::string::npos);

    const std::string canonical =
        "[CameraUnlock]\r\nConfigFormat=1\r\n[Light]\r\nLightFollowsHead=false\r\nLightMultiplier=0\r\n";
    WriteFileBytes(s.ini(), canonical);
    const auto loaded = cfg::ConfigOwner<t2_ht::Config>(t2_ht::config::OwnerOptions(
        s.game().wstring(), cfg::DefaultsFile::At(s.defaults().wstring()))).Load();
    CHECK(loaded.config.light_multiplier == 0.0f);
    CHECK(ReadFileBytes(s.ini()) == canonical);
    bool warned = false;
    for (const auto& line : loaded.log) {
        if (line.find("LightFollowsHead") != std::string::npos &&
            line.find("ignored") != std::string::npos) warned = true;
    }
    CHECK_MSG(warned, "the retired key diagnostic reports that the switch is ignored");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::strcmp(argv[1], "--render-config") == 0) {
        WriteFileBytes(argv[2], Rendered());
        return 0;
    }

    TheCommittedFileIsTheFreshRender();
    TheCommittedFileFollowsDefaultsIni();
    FirstLaunchCreatesTheCommittedFile();
    ADefaultRowFollowsDefaultsIni();
    TheYawToggleSavesItsLineAndNothingElse();
    TheModeCycleSavesThePair();
    TheLegacyFileIsImportedAndLeftAsItWas();
    AnOldYawKeyIsCarried();
    TheOldDefaultYawKeyStaysPageDown();
    AnOldYawKeyOnCtrlStaysPageDown();
    AnUntouchedSettingFollowsDefaultsIni();
    AnOldYawKeyOnEndStaysUnbound();
    TheRetiredLightSwitchIsIgnored();

    return t2_test::Report();
}
