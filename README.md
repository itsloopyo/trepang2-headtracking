# Trepang2 Head Tracking

![Trepang2 running with this mod](https://raw.githubusercontent.com/itsloopyo/trepang2-headtracking/main/assets/readme-clip.gif)

An unofficial head tracking mod for Trepang2 that moves the view with your head while your mouse or controller keeps aiming, driven by a webcam, phone, or any OpenTrack compatible tracker, with no VR headset required.

## Features

- **Decoupled look and aim** - head tracking moves the camera; aim stays on your mouse or controller
- **6DOF positional tracking** - lean and peek with head position
- **Works with any OpenTrack compatible tracker** - free options available for PC, iOS and Android

## Requirements

- [Trepang2](https://store.steampowered.com/app/1164940/Trepang2/) on Steam, or Trepang2 from the Xbox app / PC Game Pass.
- A head tracking source that can send the OpenTrack UDP protocol: [OpenTrack](https://github.com/opentrack/opentrack) with a webcam or a VR headset, or a phone app that sends it directly.
- Windows 10 or 11, 64-bit.

The mod recognises the game builds it was made for by their executable: the Steam build (menu footer `BUILD#: 2484`, Jul 30 2024) and the Xbox / Game Pass package version 1.0.15.0. On a build it does not know, it writes a line to `HeadTracking.log` and stays dormant, and the game runs exactly as it ships.

## Installation

### Lopari

Once this mod is available in Lopari, download [Lopari](https://lopari.app), choose **Trepang2**, and click
**Play with head tracking**.

### Standalone Installer

1. Download the installer ZIP from the [Releases](https://github.com/itsloopyo/trepang2-headtracking/releases) page. There is no release yet; until there is, build from source.
2. Extract it anywhere.
3. Double-click `install.cmd`.
4. Configure OpenTrack to output UDP to `127.0.0.1:4242`.
5. Launch the game.

The installer sets up one copy per run. It checks `TREPANG2_PATH`, then Steam, then the Xbox app, and installs into the first copy it finds, so on a machine with both it takes the Steam one. Point it at a folder to install into that copy instead:

```powershell
# Environment variable
$env:TREPANG2_PATH = "D:\SteamLibrary\steamapps\common\Trepang2"
.\install.cmd

# Or as an argument
.\install.cmd "D:\SteamLibrary\steamapps\common\Trepang2"
```

If you have the game on both stores, run it a second time with the other folder as the argument.

### Manual Installation

The payload goes next to the game's shipping executable:

- Steam: `CPPFPS\Binaries\Win64\` under the game folder, beside `CPPFPS-Win64-Shipping.exe`.
- Xbox / Game Pass: `Content\CPPFPS\Binaries\WinGDK\` under the folder the Xbox app installed the game to, beside `CPPFPS-WinGDK-Shipping.exe`.

No mod manager deploys this mod: they place files into one fixed subtree per game, and this one has to land beside the exe, which is why there is a single installer ZIP and no Nexus page.

From the extracted ZIP:

1. Copy `vendor\ultimate-asi-loader\dinput8.dll` into that folder and rename it to `winmm.dll`. That is an import both shipping executables already have, so it is the proxy name the loader has to use here.
2. Copy `plugins\Trepang2HeadTracking.asi` into the same folder.

`HeadTracking.ini` and `HeadTracking.log` are written to that folder on first launch.

## Setting Up OpenTrack

1. Open OpenTrack.
2. Set **Output** to `UDP over network`.
3. Open its options and set the address to `127.0.0.1` and the port to `4242`.
4. Pick an **Input** (see below), then press **Start**.

Centering is done in the tracker: OpenTrack's Center bind, the CENTER button in a phone app, or SteamVR's reset.

### VR Headset Setup

1. Connect the headset over Air Link, Virtual Desktop, or a link cable.
2. Start SteamVR.
3. Set OpenTrack's **Input** to the SteamVR tracker.
4. Leave **Output** on `UDP over network`, `127.0.0.1:4242`.

### Webcam Setup

1. Set OpenTrack's **Input** to `neuralnet tracker`, which uses the webcam alone and needs no markers, clips, or IR hardware.
2. Pick your camera in the tracker options and set the resolution and frame rate it runs at.
3. Leave **Output** on `UDP over network`, `127.0.0.1:4242`, then press **Start**.

### Phone App Setup

This mod accepts one thing: the OpenTrack UDP protocol on port `4242`. A phone tracker is usable here if it sends that protocol itself, or ships a PC-side companion that does. Check your app against that before anything else.

For an app that does send it, what decides the wiring is how much filtering it does on the phone before the packet leaves. An app that filters on-device can point straight at this PC's LAN address on port `4242`. A raw or lightly filtered feed sent direct will jitter, and that app should go through OpenTrack instead so its filters can clean the feed up first. The test is quicker than the theory: send direct, hold your head still, and if the view drifts or shakes, route it through OpenTrack.

I made [Headcam](https://headcam.app) so decent tracking was free for anybody with a phone already in their pocket. It filters on-device, so it can send direct. Any app that filters enough noise works the same way.

A phone on WiFi is a remote connection and gets `RemoteSmoothing`. So does a tracker running on this same PC that sends to the LAN address instead of `127.0.0.1`, because the mod classifies the transport rather than the machine.

## Controls

Both columns do the same thing where both are listed. Use whichever your keyboard has.

| Action              | Nav-cluster | Chord          |
|---------------------|-------------|----------------|
| Toggle tracking     | `End`       | `Ctrl+Shift+Y` |
| Cycle tracking mode | `Page Up`   | `Ctrl+Shift+J` |
| Toggle yaw mode     | `Page Down` | none           |
| Cycle ADS mode      | `Insert`    | `Ctrl+Shift+U` |

Trepang2 runs its own key bindings whether or not Ctrl and Shift are held, and it binds `G` to throwing a grenade and `H` to dual wielding by default. The chords other mods in this series use for the tracking mode and the yaw mode (`Ctrl+Shift+G` and `Ctrl+Shift+H`) would do both at once here, so the tracking mode cycle uses `Ctrl+Shift+J` instead and the yaw mode toggle has no chord. Trepang2 fires its bindings with Ctrl and Shift held, so whatever you have on those two keys also happens while the chord is down.

**Cycle tracking mode** steps through rotation and position, then rotation only, then position only.

`Insert` / `Ctrl+Shift+U` cycles what happens when you aim down sights. Not every weapon in Trepang2 can aim down sights; the game's all-weapons ADS cheat lets all of them. All three modes start the same way - raising the sights swings the view onto the point the weapon is aimed at, so your shot lands where you had it lined up - and they differ in what happens for the rest of the aim:

1. **Tracking paused** (default) - the game keeps the camera for as long as the sights are up. The sight picture is exactly the game's, and head movement does nothing until you lower the weapon.
2. **Tracking on, with an aim marker** - head tracking carries on from the snapped position, and a crosshair is drawn wherever your rounds will actually land. This crosshair is authoritative, including with scoped weapons. A scope's built-in reticle is only accurate while your eye is exactly aligned with the optic, so the two reticles separate when head tracking moves your view off that sight line.
3. **Tracking on, no aim marker** - the same as 2 without the marker, for a cleaner screen when you are happy reading the sights themselves.

The choice is saved, so it survives a restart, and the mode you switched to is named in `HeadTracking.log`.

**Toggle yaw mode** switches which axis head yaw turns about. Horizon-locked is the default: yaw goes about the world up-axis, so looking at the floor and turning your head pans across it. Camera-local turns about the camera's own up-axis instead, which leans the horizon when the camera is pitched steeply. It applies for the session and is not written back to the file.

## Configuration

`HeadTracking.ini` sits next to the game exe (`CPPFPS\Binaries\Win64\` on Steam, `Content\CPPFPS\Binaries\WinGDK\` on Game Pass) and is written with the defaults on first launch. Delete it to get the defaults back.

```ini
[Network]
; UDP port the tracker sends to. 4242 is the OpenTrack default.
Port=4242

[Tracking]
; Smoothing, 0.0 (none) to 1.0 (heaviest). LocalSmoothing applies to a
; tracker sending to 127.0.0.1; RemoteSmoothing to any other address,
; including this PC's own LAN address.
LocalSmoothing=0.00
RemoteSmoothing=0.15

[View]
; What head tracking does while you aim down sights. Insert (or
; Ctrl+Shift+U) cycles this in game and saves it here.
;   paused   - tracking stands down for the aim (default)
;   marker   - tracking stays on, with a crosshair where the rounds land
;   tracked  - tracking stays on, no crosshair
AdsMode=paused

[General]
; 1 = head yaw turns about the world's up axis (horizon stays level).
; 0 = about the camera's own up axis. Page Down toggles this for the
; session.
WorldSpaceYaw=1

[Camera]
; Stop a positional lean from putting the view inside walls.
CollisionEnabled=1
; Distance held off a surface, in centimetres (5 to 40).
CollisionMargin=10.0

[Light]
; 1 = the torch points where you are looking instead of where the
; weapon is aiming.
LightFollowsHead=1
; How far the beam turns for a given head turn, 0 to 5. 1.5 leads the
; view, so the light reaches what you turned to look at; 1.0 matches
; the view; 0 leaves the beam on the aim.
LightMultiplier=1.50

[Hotkeys]
; Virtual-key codes. End (toggle tracking), Page Up (cycle tracking
; mode) and the Ctrl+Shift chords (Y, J, U) are fixed.
YawMode=0x22
AdsMode=0x2D
```

### Field of view

Trepang2 has its own field of view slider in the video options, running from 70 to 130. The mod reads it while the game runs, so a change to it takes effect on the next frame without a restart.

Head tracking moves the picture by the same amount whatever field of view the game is drawing at. Raising the sights and looking through an optic both narrow it, and a narrower field of view magnifies everything in the frame, head movement included. The mod scales the head pose by the ratio between the field of view being drawn and the one your slider is set to, so a given head movement moves the view as far through a scope as it does walking around. Head roll is left alone, because a tilt of the picture is the same tilt at any field of view. The `fov:` line in the log carries both values and the factor between them, and reads `factor 1.0000` in ordinary play.

### The torch

The torch points along your aim, so without the mod head tracking turns the view away from the beam and leaves the light behind. `LightFollowsHead` takes it off the aim and turns it with your head instead.

It turns further than the view does - `LightMultiplier` times as far, 1.5 by default. When you turn your head you keep your eyes on the thing you turned towards, so what you are actually looking at sits past the middle of the screen, and a beam that only matched the view would land short of it. Set it to `1.0` to have the beam sit where the view is pointed, or to `0` to leave the torch on the aim, which is what the game does unmodded. A value outside 0 to 5 is refused with a line in the log and the default stands.

Nothing else about the torch changes: only which way it points is the mod's, and its brightness and its cone stay the game's. Aiming decides where the rounds go, and the beam moving does not touch that. Whenever tracking is not being applied - a menu, a loading screen, a cutscene, tracking toggled off with `End`, or the sights up in the default ADS mode - the beam goes back on the game's own aim. The `torch=` field in the log's heartbeat line says which of those it is doing.

### Window placement

A windowed game is moved once to the centre of the desktop work area on the monitor it opened on, after its window has stopped moving. That is the screen minus the taskbar, so the picture sits a little above the middle of the glass. A fullscreen or borderless window is left where it is, and so is one the game already put there - which is where an ordinary windowed launch lands, so most launches are not moved at all. Nothing is moved on a game build the mod has no profile for. The `window:` line in the log says which of those happened.

## Troubleshooting

`HeadTracking.log`, beside the game exe, records the build the mod matched, whether the hook installed, the tracker link, and every change in whether head tracking is allowed and why. Read it first.

**Mod not loading:**

- Check that `winmm.dll` and `Trepang2HeadTracking.asi` are both beside the shipping exe (see Manual Installation for the folder on each store). A copy in the game's root folder is never loaded.
- No `HeadTracking.log` at all means the loader never ran; run `install.cmd` again and let it resolve the path itself.
- On a game build this release does not know, the log says so and the mod stays dormant on purpose rather than hooking against addresses that have moved.

**No tracking response:**

- You are in a menu, the pause menu, a cutscene, the game's Kamera camera mode, dead, or loading. By design the view is left alone in all of those, and the log names which one.
- Something else has the tracker port. `link: UDP 4242 waiting-for-port` is the mod waiting for it, and the `udp: Failed to bind UDP port 4242` line above it carries the reason Windows gave. Error 10048 is another program already on the port, usually a game left running - close it and the mod takes the port on its next retry, under a second later, without you restarting anything.
- `link: UDP 4242 listening` with no `receiving` line after it means nothing is sending to the port. Check the tracker is running and pointed at this machine on the port in `HeadTracking.ini`.
- Check tracking is not switched off with `End` or `Ctrl+Shift+Y`.

**Jittery or unstable tracking:**

- A phone sending a raw feed direct is the usual cause. Route it through OpenTrack and let its filters clean it up, or raise `RemoteSmoothing`.
- On a webcam, poor or uneven lighting makes the neuralnet tracker's output noisy. Light your face evenly and avoid a bright window behind you.

**Yaw feels wrong when looking up or down at extreme angles:**

- Press `Page Down` to switch yaw mode. Horizon-locked, the default, turns head yaw about the world's up axis, so the horizon stays level however steeply the camera is pitched. Camera-local turns it about the camera's own up axis instead, which tilts the horizon as you turn while looking up or down. Press it again to go back.

**Leaning into a wall stops short:**

- By design. The lean is cut to the room the level leaves, holding the view `CollisionMargin` centimetres off the surface, and the log writes `lean-clamp: holding the view off geometry` when it does. `CollisionEnabled=0` turns that off, at the cost of seeing through walls when you lean into them.

**The crosshair moves when I turn my head:**

- By design. It moves to stay on the point your rounds will land, which is no longer the middle of the screen once your head is turned or leaning. The stealth dot, the kill marker and the tactical visor crosshair move with it. Bullet spread still scatters shots around that point the way it does around the middle of the screen without the mod.

**Known limitations:**

- Only the Steam build and the Game Pass package version named under Requirements have been tested.
- Trepang2 has no multiplayer mode, so the mod covers single player alone.

## Updating

Download the new release and run `install.cmd` again. `HeadTracking.ini` is left alone, so your settings carry over.

## Uninstalling

Run `uninstall.cmd`. This removes the mod files and its ini and logs. The loader is only removed if the installer put it there; `uninstall.cmd /force` removes it anyway.

## Building from Source

Requires Visual Studio 2022 with the C++ workload, CMake 3.20 or newer, and [pixi](https://pixi.sh).

```powershell
git clone --recursive https://github.com/itsloopyo/trepang2-headtracking.git
cd trepang2-headtracking
pixi run build
pixi run test
pixi run package
```

`pixi run package` writes the installer ZIP to `release\`. `pixi run deploy` builds and copies the result into every detected game install for a dev loop.

## Community & Support

- [Discord](https://discord.com/invite/dxyZdyFNT9) - setup help, bug reports, and new-release announcements
- [Lopari](https://lopari.app) - free Windows launcher with one-click install and launch of head-tracking mods
- [Headcam](https://headcam.app) - free app that turns your phone into a head tracker

## License

This mod is MIT licensed, copyright (c) 2026 itsloopyo. The full text is in
[LICENSE](LICENSE).

The third-party code bundled into or linked against `Trepang2HeadTracking.asi`
stays under its own licence. [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md)
lists each component with its version, licence and upstream, and reproduces the
licence texts that require it. `LICENSE` and `THIRD-PARTY-NOTICES.md` both ship
at the root of the installer ZIP.

## Credits

- [Trepang Studios](https://store.steampowered.com/app/1164940/Trepang2/) for Trepang2, published by Team17.
- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) by ThirteenAG, the loader this mod ships with.
- [OpenTrack](https://github.com/opentrack/opentrack) for the tracking protocol and the trackers that speak it.
- [MinHook](https://github.com/TsudaKageyu/minhook) by TsudaKageyu, used for the engine hooks.
- [cameraunlock-core](https://github.com/itsloopyo/cameraunlock-core), the shared head tracking library behind every mod in this series.

## Disclaimer

This mod is not affiliated with, endorsed by, or supported by Trepang Studios or Team17. Use at your own risk.
