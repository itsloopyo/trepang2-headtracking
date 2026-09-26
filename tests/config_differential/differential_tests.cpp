// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// The differential test for the conversion from HeadTracking.ini to
// CameraUnlock.ini.
//
// Three readings of every input, and what may differ between them:
//
//   Oracle     the reader of the newest published build (v0.2.0, bc82b42, core
//              76304a2), compiled from its own sources (oracle_api.h)
//   Import     the frozen reader in src/legacy_config/
//   Migration  the config owner's Load in a folder holding only the input as
//              HeadTracking.ini, which imports it through config::Import into a
//              new CameraUnlock.ini, then the canonical reader and table on it
//
// Comparison 1, oracle against import, is what a player sees change that the
// conversion did not cause: commits since the published build that change how
// the file is read. Each one is listed below with its commit.
//
// Comparison 2, import against migration, is the proof for the conversion. It
// allows one difference, the owner's ruling of 2026-09-26 that the mode and yaw
// hotkeys take the fleet's lists (NormalisedHotkeys): the file carried no pose
// shaping and no reticle setting, the reader lets through no value that is not
// finite, the yaw key it keeps is inside 0x01-0xFE, and no default moved, so the
// no-file input may not differ either.
//
// Each input migrates three times: over a Defaults.ini the owner creates with the
// built-in values, from a read-only HeadTracking.ini, and over a Defaults.ini
// that differs from the built-in value on every global row. All three give the
// settings the import read, since the migration writes `default` only where the
// imported value is what `default` gives at that launch.
//
// Inputs: the published build's first-run file (v0.1.0 and v0.2.0 shipped no
// config and seeded none, so every player's file started as that one), no file,
// an empty file, and core's corpus of mutations of the first-run file.

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "config.h"
#include "legacy_config/legacy_config.h"
#include "oracle_api.h"
#include "test_harness.h"

#include "cameraunlock/config/canonical_ini.h"
#include "cameraunlock/config/config_owner.h"
#include "cameraunlock/config/legacy_import.h"
#include "cameraunlock/config/testing/ini_mutations.h"
#include "cameraunlock/input/key_bindings.h"
#include "cameraunlock/tracking/tracking_mode.h"

namespace {

using cameraunlock::TrackingMode;
namespace cfg = cameraunlock::config;
namespace fs = std::filesystem;
namespace testing = cameraunlock::config::testing;

// ---- Provenance ------------------------------------------------------------
//
// Every source the oracle and the import compile, pinned by the SHA-256 of its
// bytes. The oracle's files are the published build's, taken with
// `git show v0.2.0:src/<file>` and `git -C cameraunlock-core show 76304a2:<path>`.
// The core files both readers compile are hash-equal to 76304a2's, so the
// readers differ only where the mod's own reader changed. The frozen import is
// pinned at the commit that froze it, so nothing edits it afterwards.

struct Pinned {
    const char* path;
    const char* sha256;
};

constexpr Pinned kPinned[] = {
    // The oracle: v0.2.0:src/...
    {"tests/config_differential/oracle/src/config.cpp", "cc24b81adb7de5075b50f5c131ab0bf13b2667c00badfc966fafcdec0e2e44b1"},
    {"tests/config_differential/oracle/src/config.h", "0d66b80ae4edc1548f189ff382fceefcbe9cb7dbe8110b1ec143ec8502d7b59c"},
    {"tests/config_differential/oracle/src/ads.h", "68d30f051e7993b40013b9760c76f08819bdd5020279333558b45b8493ce80d6"},
    {"tests/config_differential/oracle/src/logging.h", "e7578768698d872d0120e44ca15628dd8181aa05b237fc3262991e8bf3e33e8b"},
    // The oracle: 76304a2:cpp/include/cameraunlock/..., which core has changed or removed since.
    {"tests/config_differential/oracle/core/cameraunlock/ads/ads_blend.h", "bcc009fa97e0d8284a46ed5284ec0741ad3f1e8d1babe4be07bf45476351560a"},
    {"tests/config_differential/oracle/core/cameraunlock/ads/ads_fade.h", "00b80e59d261546dd50138759676f5a1b0d07681fa79a9b89be69797dc35c37f"},
    {"tests/config_differential/oracle/core/cameraunlock/ads/ads_mode.h", "94cd36b585e878e673566f602e9496417cb797970162fcc9c2af1e50e24358de"},
    {"tests/config_differential/oracle/core/cameraunlock/ads/entry_pose.h", "0c26c26fd3f6ba8307b731af391340e9286504ef157329c04883495f211cc870"},
    {"tests/config_differential/oracle/core/cameraunlock/effects/head_follow_light.h", "05c1af3befc789e7dfc665c459ee026cdd94c7a9286a805cd20f77c21606b24c"},
    // Both readers: core at the pin, hash-equal to 76304a2:cpp/...
    {"cameraunlock-core/cpp/include/cameraunlock/config/ini_reader.h", "a7ffb44210ff59672fa97e8e5feaa2cb3e81938fcc0334a384c68bc371b3857a"},
    {"cameraunlock-core/cpp/src/config/ini_reader.cpp", "e01515c2656aaf533bae4350dc45b702c3e3d4043935743dcc9bd5ea581fbe1c"},
    {"cameraunlock-core/cpp/include/cameraunlock/config/value_guards.h", "6d3e3512bdfc9ac2a54bee750c5425191bdb4c24f3ae2ea3b9925612e01c78e5"},
    {"cameraunlock-core/cpp/src/config/value_guards.cpp", "a833ff7f2721f7974ce039a3597349a0974e6695ce3e75e598ce5bf4985a14b1"},
    {"cameraunlock-core/cpp/include/cameraunlock/math/finite_utils.h", "c59772d698d54ade3374ee1221b74f5563a86d76f0eb34a989ef7efab389c0ad"},
    {"cameraunlock-core/cpp/include/cameraunlock/protocol/port_utils.h", "91bff564d5e279b66527ec5553afcf4d591812dab0e78f71a48ef412e4db7a44"},
    {"cameraunlock-core/cpp/include/cameraunlock/logging/file_log.h", "43bdd2ef8554c78e5f440333463750c13b95110fe672b0b6e273244df9e7d169"},
    {"cameraunlock-core/cpp/src/logging/file_log.cpp", "73c53c2baa06bbfebe8211f62678aa2b60cb95f604743d3686951ba56b87ea47"},
    // The import: src/logging.h is v0.2.0's, the legacy folder is frozen.
    {"src/logging.h", "e7578768698d872d0120e44ca15628dd8181aa05b237fc3262991e8bf3e33e8b"},
    {"src/legacy_config/legacy_config.h", "48435b48d884704594dbc5b1d43256b6457d5ab870142abe13f37395656c36a7"},
    {"src/legacy_config/legacy_config.cpp", "642bb5ac9f2cf8fb4b3bdbe1007745c8d91eca6197fa305ce0fb14c8abfddacb"},
};

std::string ReadFileBytes(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::string Sha256Hex(const std::string& bytes) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) {
        throw std::runtime_error("BCryptOpenAlgorithmProvider(SHA256) failed");
    }
    BCRYPT_HASH_HANDLE hash = nullptr;
    unsigned char digest[32] = {};
    const bool ok =
        BCRYPT_SUCCESS(BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0)) &&
        BCRYPT_SUCCESS(BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())),
                                      static_cast<ULONG>(bytes.size()), 0)) &&
        BCRYPT_SUCCESS(BCryptFinishHash(hash, digest, sizeof(digest), 0));
    if (hash) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(alg, 0);
    if (!ok) throw std::runtime_error("SHA-256 failed");
    static const char kHex[] = "0123456789abcdef";
    std::string out;
    for (unsigned char b : digest) {
        out += kHex[b >> 4];
        out += kHex[b & 15];
    }
    return out;
}

void SourcesAreThePinnedOnes() {
    for (const Pinned& p : kPinned) {
        const std::string actual = Sha256Hex(ReadFileBytes(std::string(T2_SOURCE_DIR) + "/" + p.path));
        if (actual != p.sha256) std::printf("  %s is %s\n", p.path, actual.c_str());
        CHECK_MSG(actual == p.sha256, p.path);
    }
}

// ---- Scratch folders ---------------------------------------------------------
//
// One folder per input: GetPrivateProfileString, which both readers sit on, is
// free to cache the file it last read. `dir` stands for the folder holding the
// game exe; Defaults.ini sits in `global` beside it.

class Scratch {
public:
    Scratch() {
        static unsigned s_next = 0;
        wchar_t temp[MAX_PATH + 1] = {};
        if (GetTempPathW(MAX_PATH + 1, temp) == 0) throw std::runtime_error("GetTempPathW failed");
        root_ = fs::path(temp) /
                ("t2_ht_diff_" + std::to_string(GetCurrentProcessId()) + "_" + std::to_string(s_next++));
        Remove();
        fs::create_directories(root_ / "game");
    }
    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;
    // A scanner can still hold a file the test just wrote, and a destructor must
    // not throw, so a folder left behind is reported and the run carries on.
    ~Scratch() {
        try {
            Remove();
        } catch (const fs::filesystem_error& e) {
            std::printf("  scratch folder left behind: %s\n", e.what());
        }
    }

    std::string dir() const { return (root_ / "game").string(); }
    std::wstring wdir() const { return (root_ / "game").wstring(); }
    std::string ini() const { return dir() + "\\HeadTracking.ini"; }
    std::wstring wini() const { return wdir() + L"\\HeadTracking.ini"; }
    fs::path canonical() const { return root_ / "game" / "CameraUnlock.ini"; }
    fs::path defaults() const { return root_ / "global" / "Defaults.ini"; }

    // Every file in the game folder, by name, with its bytes.
    std::vector<std::pair<std::string, std::string>> Listing() const {
        std::vector<std::pair<std::string, std::string>> files;
        for (const auto& entry : fs::directory_iterator(root_ / "game")) {
            files.push_back({entry.path().filename().string(), ReadFileBytes(entry.path().string())});
        }
        std::sort(files.begin(), files.end());
        return files;
    }

    std::set<std::string> Names() const {
        std::set<std::string> names;
        for (const auto& entry : fs::directory_iterator(root_ / "game")) names.insert(entry.path().filename().string());
        return names;
    }

    void Write(const std::string& bytes) const {
        std::ofstream out(ini(), std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!out) throw std::runtime_error("cannot write " + ini());
    }

    void WriteDefaults(const std::string& bytes) const {
        fs::create_directories(defaults().parent_path());
        std::ofstream out(defaults(), std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!out) throw std::runtime_error("cannot write " + defaults().string());
    }

    cfg::ConfigOwnerOptions<t2_ht::Config> Options() const {
        return t2_ht::config::OwnerOptions(wdir(), cfg::DefaultsFile::At(defaults().wstring()));
    }

private:
    // The read-only inputs lose the attribute first, so remove_all can delete them.
    void Remove() const {
        if (!fs::exists(root_)) return;
        for (const auto& entry : fs::recursive_directory_iterator(root_)) {
            SetFileAttributesW(entry.path().c_str(), FILE_ATTRIBUTE_NORMAL);
        }
        fs::remove_all(root_);
    }

    fs::path root_;
};

// ---- What a reading does -------------------------------------------------------

std::uint32_t Bits(float f) {
    std::uint32_t u;
    std::memcpy(&u, &f, sizeof u);
    return u;
}

enum Action { kToggle, kCycleMode, kYawMode, kAdsMode };

// One registered binding: the action, the virtual-key code, and the modifiers
// it needs (0, or Ctrl+Shift as cameraunlock::input::KeyModifiers spells it).
using Hotkey = std::tuple<int, int, unsigned>;
constexpr unsigned kPlain = 0;
constexpr unsigned kCtrlShift = 3;

constexpr int kVkEnd = 0x23;
constexpr int kVkPageUp = 0x21;

// Everything a reading decides that the running mod acts on: the settings, the
// state at startup, and the bindings the poller registers.
struct Observed {
    int udp_port = 0;
    bool start_enabled = false;
    bool start_world_yaw = false;
    int start_mode = 0;
    float local_smoothing = 0;
    float remote_smoothing = 0;
    bool collision_enabled = false;
    float collision_margin = 0;
    int collision_channel = 0;
    int aim_trace_channel = 0;
    float collision_release_smoothing = 0;
    bool light_follows_head = false;
    float light_multiplier = 0;
    bool dev_commands = false;
    std::vector<Hotkey> hotkeys;
};

std::vector<std::string> Differences(const Observed& a, const Observed& b) {
    std::vector<std::string> out;
    if (a.udp_port != b.udp_port) out.push_back("UDP port");
    if (a.start_enabled != b.start_enabled) out.push_back("tracking on at startup");
    if (a.start_world_yaw != b.start_world_yaw) out.push_back("yaw mode at startup");
    if (a.start_mode != b.start_mode) out.push_back("tracking mode at startup");
    if (Bits(a.local_smoothing) != Bits(b.local_smoothing)) out.push_back("local smoothing");
    if (Bits(a.remote_smoothing) != Bits(b.remote_smoothing)) out.push_back("remote smoothing");
    if (a.collision_enabled != b.collision_enabled) out.push_back("collision enabled");
    if (Bits(a.collision_margin) != Bits(b.collision_margin)) out.push_back("collision margin");
    if (a.collision_channel != b.collision_channel) out.push_back("collision channel");
    if (a.aim_trace_channel != b.aim_trace_channel) out.push_back("aim trace channel");
    if (Bits(a.collision_release_smoothing) != Bits(b.collision_release_smoothing)) {
        out.push_back("collision release smoothing");
    }
    if (a.light_follows_head != b.light_follows_head) out.push_back("light follows head");
    if (Bits(a.light_multiplier) != Bits(b.light_multiplier)) out.push_back("light multiplier");
    if (a.dev_commands != b.dev_commands) out.push_back("dev commands");
    if (a.hotkeys != b.hotkeys) out.push_back("hotkeys");
    return out;
}

// Hand copied, not compiled from the published sources: the oracle library
// exports only the reader. Copied from src/mod_hotkeys.cpp:78-98 (Register) at
// the commit the reader was frozen at, and v0.2.0:src/mod_hotkeys.cpp:93-113 less
// its ADS lines: End and Page Up NavGuarded; the configured yaw key NavGuarded
// unless it is End or Page Up, which addNav refuses; the Y and J chords
// ChordGuarded. No chord for the yaw mode.
std::vector<Hotkey> LegacyHotkeys(int yaw_mode_key) {
    std::vector<Hotkey> keys = {
        {kToggle, kVkEnd, kPlain},
        {kCycleMode, kVkPageUp, kPlain},
        {kToggle, 0x59, kCtrlShift},
        {kCycleMode, 0x4A, kCtrlShift},
    };
    if (yaw_mode_key != kVkEnd && yaw_mode_key != kVkPageUp) keys.push_back({kYawMode, yaw_mode_key, kPlain});
    std::sort(keys.begin(), keys.end());
    return keys;
}

// What the import binds for the legacy set, by the owner's ruling of 2026-09-26
// that every hotkey row follows the fleet's lists: the mode cycle's Ctrl+Shift+J,
// bound in code, becomes Ctrl+Shift+G, and the yaw key's old default, Page Down
// alone, becomes Page Down and Ctrl+Shift+H. A yaw key the player changed keeps
// its one binding, and one the build refused stays unbound.
std::vector<Hotkey> NormalisedHotkeys(int yaw_mode_key) {
    constexpr int kVkPageDown = 0x22;
    std::vector<Hotkey> keys = {
        {kToggle, kVkEnd, kPlain},
        {kCycleMode, kVkPageUp, kPlain},
        {kToggle, 0x59, kCtrlShift},
        {kCycleMode, 0x47, kCtrlShift},
    };
    if (yaw_mode_key == kVkPageDown) {
        keys.push_back({kYawMode, kVkPageDown, kPlain});
        keys.push_back({kYawMode, 0x48, kCtrlShift});
    } else if (yaw_mode_key != kVkEnd && yaw_mode_key != kVkPageUp) {
        keys.push_back({kYawMode, yaw_mode_key, kPlain});
    }
    std::sort(keys.begin(), keys.end());
    return keys;
}

// Hand copied from v0.2.0:src/view_hook.cpp:79 (g_trackingEnabled starts true),
// v0.2.0:src/view_hook.cpp:615 (the yaw mode from the config) and core 76304a2's
// head_tracking_session.h:471 (the session starts in rotation and position,
// which nothing in the mod changes before the first key press). The frozen
// reader's commit has the same three at src/view_hook.cpp:77 and :604.
constexpr bool kLegacyStartEnabled = true;
constexpr TrackingMode kLegacyStartMode = TrackingMode::RotationAndPosition;

struct OracleReading {
    Observed observed;
    int ads_mode = 0;
};

OracleReading ReadOracle(const std::string& dir) {
    const t2_oracle::PublishedConfig c = t2_oracle::Load(dir);
    OracleReading r;
    Observed& o = r.observed;
    o.udp_port = c.udp_port;
    o.start_enabled = kLegacyStartEnabled;
    o.start_world_yaw = c.world_space_yaw;
    o.start_mode = static_cast<int>(kLegacyStartMode);
    o.local_smoothing = c.local_smoothing;
    o.remote_smoothing = c.remote_smoothing;
    o.collision_enabled = c.collision_enabled;
    o.collision_margin = c.collision_margin;
    o.collision_channel = c.collision_channel;
    o.aim_trace_channel = c.aim_trace_channel;
    o.collision_release_smoothing = c.collision_release_smoothing;
    o.light_follows_head = c.light_follows_head;
    o.light_multiplier = c.light_multiplier;
    o.dev_commands = c.dev_commands;
    // v0.2.0:src/mod_hotkeys.cpp:93-113 (Register): the frozen reader's set, plus
    // the ADS cycle on its configured key (line 107, refused by addNav when it is
    // End, Page Up or the yaw key it just bound) and Ctrl+Shift+U (line 113).
    o.hotkeys = LegacyHotkeys(c.yaw_mode_key);
    const bool yaw_bound = c.yaw_mode_key != kVkEnd && c.yaw_mode_key != kVkPageUp;
    const bool ads_refused = c.ads_mode_key == kVkEnd || c.ads_mode_key == kVkPageUp ||
                             (yaw_bound && c.ads_mode_key == c.yaw_mode_key);
    if (!ads_refused) o.hotkeys.push_back({kAdsMode, c.ads_mode_key, kPlain});
    o.hotkeys.push_back({kAdsMode, 0x55, kCtrlShift});
    std::sort(o.hotkeys.begin(), o.hotkeys.end());
    r.ads_mode = c.ads_mode;
    return r;
}

Observed ObserveLegacy(const t2_ht::legacy::Config& c) {
    Observed o;
    o.udp_port = c.udp_port;
    o.start_enabled = kLegacyStartEnabled;
    o.start_world_yaw = c.world_space_yaw;
    o.start_mode = static_cast<int>(kLegacyStartMode);
    o.local_smoothing = c.local_smoothing;
    o.remote_smoothing = c.remote_smoothing;
    o.collision_enabled = c.collision_enabled;
    o.collision_margin = c.collision_margin;
    o.collision_channel = c.collision_channel;
    o.aim_trace_channel = c.aim_trace_channel;
    o.collision_release_smoothing = c.collision_release_smoothing;
    o.light_follows_head = c.light_follows_head;
    o.light_multiplier = c.light_multiplier;
    o.dev_commands = c.dev_commands;
    o.hotkeys = LegacyHotkeys(c.yaw_mode_key);
    return o;
}

// ---- Inputs --------------------------------------------------------------------

std::string DataPath(const char* name) {
    return std::string(T2_SOURCE_DIR) + "/tests/config_differential/data/" + name;
}

// The published build's first-run file, extracted once from the oracle's
// WriteDefaultIfMissing and committed.
std::string FirstRunFile() { return ReadFileBytes(DataPath("v0.2.0-first-run.ini")); }

// Every key the frozen reader reads, and how the corpus varies each one. The
// out-of-range values sit either side of the range each key is refused outside.
std::vector<testing::MutationKey> CorpusKeys() {
    return {
        {"Network", "Port", "5771", {"80", "70000"}},
        {"Tracking", "LocalSmoothing", "0.3", {"-0.5", "1.5"}},
        {"Tracking", "RemoteSmoothing", "0.6", {"-0.5", "1.5"}},
        {"General", "WorldSpaceYaw", "false", {}},
        {"Hotkeys", "YawMode", "0x2E", {"0x1FF"}, true},
        {"Camera", "CollisionEnabled", "false", {}},
        {"Camera", "CollisionMargin", "20.0", {"4.0", "41.0"}},
        {"Camera", "CollisionChannel", "2", {"-1", "32"}},
        {"Camera", "CollisionReleaseSmoothing", "0.5", {"-0.5", "1.5"}},
        {"Camera", "AimTraceChannel", "3", {"-1", "32"}},
        {"Light", "LightFollowsHead", "false", {}},
        {"Light", "LightMultiplier", "2.25", {"-0.5", "5.5"}},
        {"Dev", "DevCommands", "true", {}},
    };
}

// The generator refuses the call when these and the descriptors name different
// keys, so the corpus covers every key the import reads.
std::vector<cfg::LegacyKey> CorpusReads() { return t2_ht::config::Import().keys; }

struct Input {
    std::string name;
    bool present;
    std::string bytes;
};

std::vector<Input> Inputs() {
    std::vector<Input> inputs = {
        {"v0.2.0 first-run file", true, FirstRunFile()},
        {"no file", false, {}},
        {"empty file", true, {}},
    };
    for (testing::IniMutation& m : testing::GenerateIniMutations(FirstRunFile(), CorpusReads(), CorpusKeys())) {
        inputs.push_back({"corpus: " + m.name, true, std::move(m.bytes)});
    }
    return inputs;
}

// ---- Checks --------------------------------------------------------------------

// The first-run file committed as test data is what the published build writes.
void FirstRunFileIsThePublishedBuilds() {
    Scratch s;
    t2_oracle::WriteDefaultIfMissing(s.dir());
    CHECK_MSG(ReadFileBytes(s.ini()) == FirstRunFile(),
              "v0.2.0-first-run.ini is what the published build writes at first run");
}

// Comparison 1. What the published build did that the import does not, each
// with the commit that changed it:
//
// - faa4054 (feat: head tracking stays on through aim down sights) removed the
//   ADS mode cycle. The published build read [View] AdsMode (paused, marker or
//   tracked) as the mode it started in, and [Hotkeys] AdsMode as the key that
//   cycled it, beside Ctrl+Shift+U. The import reads neither: head tracking
//   stays on through the aim, and Insert and Ctrl+Shift+U do nothing.
//
// Nothing else may differ, floats bit for bit.
void OracleAgainstImport(const std::vector<Input>& inputs) {
    int compared = 0;
    for (const Input& input : inputs) {
        Scratch s;
        if (input.present) s.Write(input.bytes);

        const OracleReading oracle = ReadOracle(s.dir());
        t2_ht::legacy::Config imported;
        t2_ht::legacy::Load(s.dir(), imported);
        const Observed import = ObserveLegacy(imported);

        Observed published = oracle.observed;
        published.hotkeys.erase(std::remove_if(published.hotkeys.begin(), published.hotkeys.end(),
                                               [](const Hotkey& h) { return std::get<0>(h) == kAdsMode; }),
                                published.hotkeys.end());
        CHECK_MSG(oracle.ads_mode >= 0 && oracle.ads_mode <= 2, "the published build started paused, marker or tracked");

        const std::vector<std::string> diff = Differences(published, import);
        for (const std::string& d : diff) std::printf("  comparison 1, %s: %s\n", input.name.c_str(), d.c_str());
        CHECK_MSG(diff.empty(), "comparison 1: oracle and import agree apart from faa4054's ADS keys");
        ++compared;
    }
    std::printf("comparison 1: %d inputs\n", compared);
}

Observed ObserveCanonical(const t2_ht::Config& c) {
    Observed o;
    o.udp_port = c.udp_port;
    // headtracking_mod.cpp LoadSettings and tracking.cpp Start.
    o.start_enabled = c.enable_on_startup;
    o.start_world_yaw = c.world_space_yaw;
    o.start_mode = static_cast<int>(cameraunlock::DecodeTrackingMode(c.rotation_enabled, c.position_enabled).value());
    o.local_smoothing = c.local_smoothing;
    o.remote_smoothing = c.remote_smoothing;
    o.collision_enabled = c.collision_enabled;
    o.collision_margin = c.collision_margin;
    o.collision_channel = c.collision_channel;
    o.aim_trace_channel = c.aim_trace_channel;
    o.collision_release_smoothing = c.collision_release_smoothing;
    o.light_follows_head = c.light_follows_head;
    o.light_multiplier = c.light_multiplier;
    o.dev_commands = c.dev_commands;
    // mod_hotkeys.cpp Register: each list through ParseKeyBindings and
    // RegisterKeyBindings.
    const std::pair<Action, const std::string*> lists[] = {
        {kToggle, &c.toggle_key}, {kCycleMode, &c.cycle_tracking_mode_key}, {kYawMode, &c.yaw_mode_key}};
    for (const auto& [action, list] : lists) {
        const cameraunlock::input::KeyBindingsParseResult parsed = cameraunlock::input::ParseKeyBindings(*list);
        CHECK_MSG(parsed.ok(), "a migrated key list parses");
        for (const cameraunlock::input::KeyBinding& b : parsed.bindings) {
            o.hotkeys.push_back({action, b.vk, static_cast<unsigned>(b.modifiers)});
        }
    }
    std::sort(o.hotkeys.begin(), o.hotkeys.end());
    return o;
}

std::vector<std::string> CanonicalDiagnostics(const std::string& bytes, t2_ht::Config& out) {
    std::vector<std::string> found;
    const cfg::CanonicalIni doc = cfg::ParseCanonicalIni(bytes);
    for (const cfg::CanonicalDiagnostic& d : doc.diagnostics) found.push_back("reader: " + cfg::DescribeCanonicalDiagnostic(d));
    const cfg::ConfigTable<t2_ht::Config> table = t2_ht::config::Table();
    out = table.defaults();
    for (const cfg::CanonicalDiagnostic& d : cfg::ApplyCanonical(doc, table, out).diagnostics) {
        found.push_back("table: " + cfg::DescribeCanonicalDiagnostic(d));
    }
    return found;
}

bool AsciiCrlf(const std::string& bytes) {
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(bytes[i]);
        if (c > 0x7E) return false;
        if (c == '\r' && (i + 1 == bytes.size() || bytes[i + 1] != '\n')) return false;
        if (c == '\n' && (i == 0 || bytes[i - 1] != '\r')) return false;
        if (c < 0x20 && c != '\r' && c != '\n') return false;
    }
    return !bytes.empty() && bytes.back() == '\n';
}

// A file's bytes, last write time and attributes, which no load may change.
struct FileState {
    std::string bytes;
    unsigned long long written = 0;
    DWORD attributes = 0;
    bool operator==(const FileState& other) const {
        return bytes == other.bytes && written == other.written && attributes == other.attributes;
    }
};

std::optional<FileState> StateOf(const fs::path& path) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND) return std::nullopt;
        throw std::runtime_error("cannot read the attributes of " + path.string());
    }
    FileState state;
    state.bytes = ReadFileBytes(path.string());
    state.written = (static_cast<unsigned long long>(data.ftLastWriteTime.dwHighDateTime) << 32) |
                    data.ftLastWriteTime.dwLowDateTime;
    state.attributes = data.dwFileAttributes;
    return state;
}

bool LogSays(const std::vector<std::string>& log, const std::string& text) {
    for (const std::string& line : log) {
        if (line.find(text) != std::string::npos) return true;
    }
    return false;
}

// A Defaults.ini holding a value other than the built-in one on every global row
// the table binds, so a migration that wrote `default` where the imported value
// is not what `default` gives would read back differently over it.
const char* const kSkewedDefaults =
    "[CameraUnlock]\r\nConfigFormat=1\r\n\r\n"
    "[Network]\r\nUdpPort=5252\r\n\r\n"
    "[General]\r\nEnableOnStartup=false\r\nWorldSpaceYaw=false\r\nRotationEnabled=false\r\n\r\n"
    "[Smoothing]\r\nLocalSmoothing=0.5\r\nRemoteSmoothing=0.5\r\n\r\n"
    "[Position]\r\nPositionEnabled=true\r\nCollisionEnabled=false\r\nCollisionReleaseSmoothing=0.25\r\n\r\n"
    "[Hotkeys]\r\nToggleKey=F8\r\nCycleTrackingModeKey=F9\r\nYawModeKey=F10\r\n\r\n"
    "[Light]\r\nLightFollowsHead=false\r\nLightMultiplier=3.5\r\n";

// The folder beside this executable the migrated files are written to, for
// lint-migrated.mjs, which CTest runs after this test.
fs::path MigratedFolder() {
    std::vector<wchar_t> exe(MAX_PATH);
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size()));
        if (length == 0) throw std::runtime_error("cannot find this executable's path");
        if (length < exe.size()) return fs::path(std::wstring(exe.data(), length)).parent_path() / "migrated";
        exe.resize(exe.size() * 2);
    }
}

// Runs the owner's Load in `s`, whose game folder holds the input as
// HeadTracking.ini or nothing, checks what a load must do beyond comparison 2,
// and returns the settings the session runs on. A file it creates by migrating
// goes into `migrated_files`.
std::optional<t2_ht::Config> Migrate(const Input& input, const Scratch& s, const std::string& label,
                                     std::set<std::string>& migrated_files) {
    const char* name = label.c_str();
    const fs::path legacy = fs::path(s.wini());
    const std::optional<FileState> legacy_before = StateOf(legacy);
    const std::set<std::string> both{"CameraUnlock.ini", "HeadTracking.ini"};

    const cfg::ConfigLoadResult<t2_ht::Config> loaded = cfg::ConfigOwner<t2_ht::Config>(s.Options()).Load();
    const cfg::ConfigLoadStatus want = input.present ? cfg::ConfigLoadStatus::Migrated : cfg::ConfigLoadStatus::Created;
    if (loaded.status != want) {
        std::printf("  %s: %s, %s\n", name, cfg::ConfigLoadStatusName(loaded.status), loaded.reason.c_str());
    }
    CHECK_MSG(loaded.status == want, "every legacy input imports, and no file is created");
    CHECK_MSG(StateOf(legacy) == legacy_before, "a load leaves HeadTracking.ini's bytes, write time and attributes");
    if (loaded.status != want) return std::nullopt;
    CHECK_MSG(s.Names() == (input.present ? both : std::set<std::string>{"CameraUnlock.ini"}),
              "the game folder holds HeadTracking.ini and CameraUnlock.ini and nothing else");

    const std::string migrated = ReadFileBytes(s.canonical().string());
    CHECK_MSG(cfg::HasCanonicalStamp(migrated), "CameraUnlock.ini carries the stamp");
    CHECK_MSG(AsciiCrlf(migrated), "CameraUnlock.ini is ASCII with CRLF line ends");
    t2_ht::Config reread;
    const std::vector<std::string> diagnostics = CanonicalDiagnostics(migrated, reread);
    for (const std::string& d : diagnostics) std::printf("  %s: CameraUnlock.ini, %s\n", name, d.c_str());
    CHECK_MSG(diagnostics.empty(), "CameraUnlock.ini reads with no diagnostic");
    if (input.present) migrated_files.insert(migrated);

    // The next start reads CameraUnlock.ini, imports nothing and writes nothing.
    const std::optional<FileState> created = StateOf(s.canonical());
    const cfg::ConfigLoadResult<t2_ht::Config> again = cfg::ConfigOwner<t2_ht::Config>(s.Options()).Load();
    CHECK_MSG(again.status == cfg::ConfigLoadStatus::Canonical, "the next start reads CameraUnlock.ini");
    CHECK_MSG(Differences(ObserveCanonical(again.config), ObserveCanonical(loaded.config)).empty(),
              "the next start runs on the same settings");
    CHECK_MSG(StateOf(s.canonical()) == created && StateOf(legacy) == legacy_before,
              "the next start changes neither file");
    CHECK_MSG(!input.present || LogSays(again.log, "is left as it was and is not read"),
              "the next start logs that HeadTracking.ini is not read");
    return loaded.config;
}

// Comparison 2, and what the migration must do with every input besides.
void ImportAgainstMigration(const std::vector<Input>& inputs) {
    const std::string committed = ReadFileBytes(std::string(T2_SOURCE_DIR) + "/CameraUnlock.ini");
    const cfg::ConfigTable<t2_ht::Config> table = t2_ht::config::Table();
    std::set<std::string> migrated_files;
    int compared = 0;
    for (const Input& input : inputs) {
        const char* name = input.name.c_str();

        // The import, run as the owner runs it but on a read-only copy: it reads
        // what the frozen reader reads, drops nothing, and writes nothing.
        cfg::ImportResult imported;
        t2_ht::legacy::Config read;
        {
            Scratch ro;
            if (input.present) {
                ro.Write(input.bytes);
                SetFileAttributesA(ro.ini().c_str(), FILE_ATTRIBUTE_READONLY);
            }
            const auto before = ro.Listing();
            t2_ht::Config unused = table.defaults();
            imported = t2_ht::config::Import().run({ro.wini(), ro.ini(), false}, unused);
            CHECK_MSG(ro.Listing() == before, "the import leaves a read-only folder as it was");
            t2_ht::legacy::Load(ro.dir(), read);
        }
        CHECK_MSG(imported.status == (input.present ? cfg::ImportStatus::Imported : cfg::ImportStatus::Absent),
                  "the import reads every input, as the published build did");
        CHECK_MSG(imported.dropped.empty() && imported.pose_shaping.empty(),
                  "comparison 2: the import drops nothing and reads no pose shaping");
        Observed want = ObserveLegacy(read);
        want.hotkeys = NormalisedHotkeys(read.yaw_mode_key);

        // Over a Defaults.ini the owner creates with the built-in values.
        Scratch s;
        if (input.present) s.Write(input.bytes);
        const std::optional<t2_ht::Config> migrated = Migrate(input, s, input.name, migrated_files);
        if (migrated) {
            const std::vector<std::string> diff = Differences(want, ObserveCanonical(*migrated));
            for (const std::string& d : diff) std::printf("  comparison 2, %s: %s\n", name, d.c_str());
            CHECK_MSG(diff.empty(), "comparison 2: the migration runs as the import read");

            // Over the built-in values the table's own defaults stand for Defaults.ini.
            t2_ht::Config reread;
            CanonicalDiagnostics(ReadFileBytes(s.canonical().string()), reread);
            CHECK_MSG(Differences(ObserveCanonical(reread), ObserveCanonical(*migrated)).empty(),
                      "CameraUnlock.ini reads back as the settings the session runs on");

            // Fresh equals upgrade: the published build's first-run file, and no
            // file at all, both end as the committed file.
            if (input.name == "v0.2.0 first-run file" || input.name == "no file") {
                CHECK_MSG(ReadFileBytes(s.canonical().string()) == committed,
                          "the first-run file and no file both give the committed file");
            }
        }

        // From a read-only HeadTracking.ini, which keeps its attribute.
        if (input.present) {
            Scratch ro;
            ro.Write(input.bytes);
            SetFileAttributesA(ro.ini().c_str(), FILE_ATTRIBUTE_READONLY);
            const std::optional<t2_ht::Config> c = Migrate(input, ro, input.name + " (read-only)", migrated_files);
            CHECK_MSG(c && Differences(want, ObserveCanonical(*c)).empty(),
                      "a read-only HeadTracking.ini imports as a writable one does");
            CHECK_MSG((GetFileAttributesA(ro.ini().c_str()) & FILE_ATTRIBUTE_READONLY) != 0,
                      "HeadTracking.ini keeps its read-only attribute");
        }

        // Over a Defaults.ini that differs everywhere. With no legacy file the
        // settings are Defaults.ini's own, so only an input with a file is held
        // to the import there.
        if (input.present) {
            Scratch skewed;
            skewed.Write(input.bytes);
            skewed.WriteDefaults(kSkewedDefaults);
            const std::optional<t2_ht::Config> c =
                Migrate(input, skewed, input.name + " (skewed Defaults.ini)", migrated_files);
            const std::vector<std::string> diff =
                c ? Differences(want, ObserveCanonical(*c)) : std::vector<std::string>{"the load"};
            for (const std::string& d : diff) std::printf("  comparison 2, %s (skewed Defaults.ini): %s\n", name, d.c_str());
            CHECK_MSG(diff.empty(), "the migration gives the import's settings over a Defaults.ini that differs everywhere");
        }
        ++compared;
    }
    std::printf("comparison 2: %d inputs\n", compared);

    // Core's canonical config lint runs over these next (lint-migrated.mjs).
    const fs::path lint = MigratedFolder();
    fs::remove_all(lint);
    fs::create_directories(lint);
    std::size_t n = 0;
    for (const std::string& file : migrated_files) {
        std::ofstream out(lint / (std::to_string(n++) + ".ini"), std::ios::binary | std::ios::trunc);
        out.write(file.data(), static_cast<std::streamsize>(file.size()));
        if (!out) throw std::runtime_error("cannot write a migrated file under " + lint.string());
    }
    std::printf("%zu distinct migrated files written to %s\n", migrated_files.size(), lint.string().c_str());
}

}  // namespace

int main() {
    // Unbuffered, so the lines before an uncaught exception reach the log.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SourcesAreThePinnedOnes();
    FirstRunFileIsThePublishedBuilds();
    const std::vector<Input> inputs = Inputs();
    OracleAgainstImport(inputs);
    ImportAgainstMigration(inputs);
    return t2_test::Report();
}
