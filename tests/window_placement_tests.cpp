// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Whether a player's game window gets moved, and where to.
//
// The rects are the ones Trepang2 produced on a 5120x1440 monitor with a 48 px
// taskbar along the bottom: a 1936x1119 window around a 1920x1080 client, 8 px
// of frame either side and a 31 px title bar.

#include "test_harness.h"
#include "window_placement.h"

namespace {

using t2_ht::window_placement::Decide;
using t2_ht::window_placement::Placement;

constexpr RECT kWork{0, 0, 5120, 1392};

RECT At(long x, long y, long w, long h) { return {x, y, x + w, y + h}; }

// A 1920x1080 client inside its frame, with the window origin at (x, y).
struct Framed {
    RECT window;
    RECT client;
};
Framed Windowed1080p(long x, long y) {
    return {At(x, y, 1936, 1119), At(x + 8, y + 31, 1920, 1080)};
}

// Where the engine put the window on an ordinary windowed launch. Its client is
// centred on the work area and its rect is 11 px higher than a centred window
// RECT would be, so this is the case that decides which of the two gets centred:
// moving it would drop every ordinary launch by those 11 px.
void TestTheGamesOwnPlacementIsLeftAlone() {
    const Framed f = Windowed1080p(1592, 125);
    POINT target{};
    CHECK(Decide(f.window, f.client, kWork, target) == Placement::AlreadyCentered);
}

// -windowed -ResX=1280 -ResY=720, also placed by the engine.
void TestASmallerWindowTheGameCentredIsLeftAlone() {
    const RECT window = At(1912, 305, 1296, 759);
    const RECT client = At(1920, 336, 1280, 720);
    POINT target{};
    CHECK(Decide(window, client, kWork, target) == Placement::AlreadyCentered);
}

// -WinX=50 -WinY=60 puts the client there. It goes to where an ordinary launch
// would have put it.
void TestAnOffCentreWindowMovesToTheGamesOwnSpot() {
    const Framed f = Windowed1080p(42, 29);
    POINT target{};
    CHECK(Decide(f.window, f.client, kWork, target) == Placement::Move);
    CHECK(target.x == 1592);
    CHECK(target.y == 125);
}

void TestOffByAPixelIsAlreadyCentred() {
    const Framed f = Windowed1080p(1593, 124);
    POINT target{};
    CHECK(Decide(f.window, f.client, kWork, target) == Placement::AlreadyCentered);
}

void TestThreePixelsOutIsMoved() {
    const Framed f = Windowed1080p(1592, 128);
    POINT target{};
    CHECK(Decide(f.window, f.client, kWork, target) == Placement::Move);
}

// -windowed -ResY=1440 on a 1392-tall work area: the title bar is already off
// the top of the screen and no position on the work area shows the whole window.
void TestAWindowTallerThanTheWorkAreaIsLeftAlone() {
    const RECT window = At(1592, -31, 1936, 1479);
    const RECT client = At(1600, 0, 1920, 1440);
    POINT target{};
    CHECK(Decide(window, client, kWork, target) == Placement::DoesNotFit);
}

// Fullscreen and borderless: the window covers the monitor, taskbar included,
// so it is wider and taller than the work area and the game's own placement
// stands.
void TestFullscreenIsLeftAlone() {
    const RECT monitor = At(0, 0, 5120, 1440);
    POINT target{};
    CHECK(Decide(monitor, monitor, kWork, target) == Placement::DoesNotFit);
}

// A window that fits with 11 px to spare: centring the picture alone would put
// the title bar 6 px above the top of the work area, where it cannot be grabbed.
void TestTheTitleBarStaysOnTheWorkArea() {
    const RECT work = At(0, 0, 2560, 1130);
    const Framed f = Windowed1080p(100, 5);
    POINT target{};
    CHECK(Decide(f.window, f.client, work, target) == Placement::Move);
    CHECK_MSG(target.y == 0, "window top clamped to the work area's top edge");
    CHECK(target.y + 1119 <= work.bottom);
}

// A second monitor to the right, with its taskbar docked on the left. The window
// is centred on THAT monitor's work area, not dragged back to the primary.
void TestAWorkAreaAwayFromTheOriginIsHonoured() {
    const RECT work = At(5120 + 60, 0, 2560 - 60, 1440);
    const Framed f = Windowed1080p(5200, 10);
    POINT target{};
    CHECK(Decide(f.window, f.client, work, target) == Placement::Move);
    CHECK(target.x == 5180 + (2500 - 1920) / 2 - 8);
    CHECK(target.y == (1440 - 1080) / 2 - 31);
}

}  // namespace

int main() {
    TestTheGamesOwnPlacementIsLeftAlone();
    TestASmallerWindowTheGameCentredIsLeftAlone();
    TestAnOffCentreWindowMovesToTheGamesOwnSpot();
    TestOffByAPixelIsAlreadyCentred();
    TestThreePixelsOutIsMoved();
    TestAWindowTallerThanTheWorkAreaIsLeftAlone();
    TestFullscreenIsLeftAlone();
    TestTheTitleBarStaysOnTheWorkArea();
    TestAWorkAreaAwayFromTheOriginIsHonoured();

    return t2_test::Report();
}
