// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "build_registry.h"

#include <array>
#include <cstring>
#include <string>

#include <cameraunlock/memory/pe_fingerprint.h>
#include <cameraunlock/os/module_paths.h>

#include "logging.h"

namespace t2_ht::builds
{
    // One extern per known build. Never-delete policy: when a game patch breaks
    // the current build, derive new RVAs and ADD a new profile here (newest at
    // the top of kKnownProfiles) without removing the old one. Users on the
    // un-patched build still match their old profile by PE fingerprint.
    extern const BuildProfile kGdkProfile_20260318;
    extern const BuildProfile kSteamProfile_20240730;

    namespace
    {
        // Newest-first. The newest profile of the running exe's own store is
        // the one an unmatched exe is read against: Trepang2's TimeDateStamp is
        // a real build date, so above or below it says whether the game or the
        // mod is the one that is behind. Comparing a Steam exe with the Game
        // Pass build (or the other way round) would say nothing, which is why
        // the reference is chosen per store.
        constexpr std::array<const BuildProfile*, 2> kKnownProfiles = {
            &kGdkProfile_20260318,
            &kSteamProfile_20240730,
        };

        const BuildProfile* g_active = nullptr;

        // A profile is "complete" iff its hook target RVA is non-zero. Lets a
        // profile with the correct fingerprint but RVAs still TBD register
        // without risking activation against stale/zero addresses.
        bool ProfileIsComplete(const BuildProfile* p)
        {
            return p && p->Offsets.kGetPlayerViewPointRva != 0;
        }

        // Profile names start with their store: "steam-win64-..." or
        // "gdk-wingdk-...". The exe name says which store is running: the Game
        // Pass build ships CPPFPS-WinGDK-Shipping.exe, Steam CPPFPS-Win64-Shipping.exe.
        const char* RunningStorePrefix()
        {
            const std::wstring exe = cameraunlock::os::ModuleFilePath(nullptr);
            return exe.find(L"WinGDK") != std::wstring::npos ? "gdk-" : "steam-";
        }

        const BuildProfile* NewestForStore(const char* prefix)
        {
            for (const BuildProfile* p : kKnownProfiles)
                if (std::strncmp(p->Name, prefix, std::strlen(prefix)) == 0) return p;
            return kKnownProfiles.front();
        }
    }

    MatchResult SelectProfile(HMODULE host)
    {
        PeFingerprint running{};
        if (!cameraunlock::memory::ReadPeFingerprint(host, running)) {
            Log::Line("build-check: failed to read PE header from host module");
            return MatchResult::ReadFailed;
        }

        Log::Line("build-check: running  ts=0x%08x size=0x%08x csum=0x%08x",
            running.TimeDateStamp, running.SizeOfImage, running.CheckSum);

        // Only the profile that claims this exe is written to the log. The
        // registry is append-only and grows for the life of the mod, so a line
        // per known build would put the one fact a triage needs behind a list
        // of builds the player is not running.
        for (const BuildProfile* p : kKnownProfiles) {
            if (!running.Matches(p->Fingerprint)) continue;
            if (!ProfileIsComplete(p)) {
                Log::Line("build-check: fingerprint matches %s but its offsets "
                          "are not yet derived - staying dormant", p->Name);
                return MatchResult::HostDiffers;
            }
            g_active = p;
            Log::Line("build-check: matched profile %s", p->Name);
            return MatchResult::Matched;
        }

        const BuildProfile* primary = NewestForStore(RunningStorePrefix());
        Log::Line("build-check: no profile claims this exe (%zu known); newest for this "
                  "store is %s ts=0x%08x size=0x%08x csum=0x%08x",
            kKnownProfiles.size(), primary->Name, primary->Fingerprint.TimeDateStamp,
            primary->Fingerprint.SizeOfImage, primary->Fingerprint.CheckSum);

        switch (cameraunlock::memory::ClassifyMismatch(running, primary->Fingerprint)) {
            case cameraunlock::memory::FingerprintMismatch::Differs:
                Log::Line("build-check: the exe carries a known build's timestamp with a "
                          "different size or checksum, so it has been repacked or "
                          "modified - this mod does not engage on a modified binary");
                return MatchResult::HostDiffers;
            case cameraunlock::memory::FingerprintMismatch::Newer:
                Log::Line("build-check: the game is newer than any build this mod knows "
                          "about - check the releases page for an updated mod");
                return MatchResult::HostUnknown;
            case cameraunlock::memory::FingerprintMismatch::Older:
                Log::Line("build-check: the game is older than the build this mod was made "
                          "for - let the store finish updating the game");
                return MatchResult::HostUnknown;
        }
        return MatchResult::HostUnknown;
    }

    const BuildProfile& ActiveProfile() { return *g_active; }
}
