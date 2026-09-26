// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// The differential test for the HeadTracking.ini conversion.
//
// Two readings of every input so far, and what may differ between them:
//
//   Oracle     the reader of the newest published build (v0.2.0, bc82b42, core
//              76304a2), compiled from its own sources (oracle_api.h)
//   Import     the frozen reader in src/legacy_config/
//
// Comparison 1, oracle against import, is what a player sees change that the
// conversion did not cause: commits since the published build that change how
// the file is read. Each one is listed below with its commit.
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
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "legacy_config/legacy_config.h"
#include "oracle_api.h"
#include "test_harness.h"

#include "cameraunlock/config/legacy_import.h"
#include "cameraunlock/config/testing/ini_mutations.h"
#include "cameraunlock/tracking/tracking_mode.h"

namespace {

using cameraunlock::TrackingMode;
namespace cfg = cameraunlock::config;
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
// free to cache the file it last read.

class Scratch {
public:
    Scratch() {
        static unsigned s_next = 0;
        char temp[MAX_PATH] = {};
        GetTempPathA(MAX_PATH, temp);
        dir_ = std::string(temp) + "t2_ht_diff_" + std::to_string(GetCurrentProcessId()) + "_" +
               std::to_string(s_next++);
        if (!CreateDirectoryA(dir_.c_str(), nullptr)) {
            throw std::runtime_error("cannot create " + dir_ + ", error " + std::to_string(GetLastError()));
        }
    }
    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;
    ~Scratch() {
        WIN32_FIND_DATAA found;
        const HANDLE h = FindFirstFileA((dir_ + "\\*").c_str(), &found);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                const std::string file = dir_ + "\\" + found.cFileName;
                SetFileAttributesA(file.c_str(), FILE_ATTRIBUTE_NORMAL);
                DeleteFileA(file.c_str());
            } while (FindNextFileA(h, &found));
            FindClose(h);
        }
        RemoveDirectoryA(dir_.c_str());
    }

    const std::string& dir() const { return dir_; }
    std::string ini() const { return dir_ + "\\HeadTracking.ini"; }

    void Write(const std::string& bytes) const {
        std::ofstream out(ini(), std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!out) throw std::runtime_error("cannot write " + ini());
    }

private:
    std::string dir_;
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

// Every key the frozen reader reads, in its order. The generator refuses the
// call when these and the descriptors name different keys.
std::vector<cfg::LegacyKey> CorpusReads() {
    return {
        {"Network", "Port"},
        {"Tracking", "LocalSmoothing"},
        {"Tracking", "RemoteSmoothing"},
        {"General", "WorldSpaceYaw"},
        {"Hotkeys", "YawMode"},
        {"Camera", "CollisionEnabled"},
        {"Camera", "CollisionMargin"},
        {"Camera", "CollisionChannel"},
        {"Camera", "CollisionReleaseSmoothing"},
        {"Camera", "AimTraceChannel"},
        {"Light", "LightFollowsHead"},
        {"Light", "LightMultiplier"},
        {"Dev", "DevCommands"},
    };
}

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

}  // namespace

int main() {
    SourcesAreThePinnedOnes();
    FirstRunFileIsThePublishedBuilds();
    const std::vector<Input> inputs = Inputs();
    OracleAgainstImport(inputs);
    return t2_test::Report();
}
