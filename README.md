# Trepang2 Head Tracking

![Trepang2 running with this mod](https://raw.githubusercontent.com/itsloopyo/trepang2-headtracking/main/assets/readme-clip.gif)

An unofficial head tracking mod for Trepang2 that moves the view with your head while your mouse or controller keeps aiming, driven by a webcam, phone, or any OpenTrack compatible tracker, with no VR headset required.

## Features

- **Decoupled look and aim** - head tracking moves the camera; aim stays on your mouse or controller
- **6DOF positional tracking** - lean and peek with head position
- **Works with any OpenTrack compatible tracker** - free options available for PC, iOS and Android

## Requirements

- [Trepang2](https://store.steampowered.com/app/1164940/Trepang2/) on Steam, Trepang2 on GOG, or Trepang2 from Xbox Game Pass.
- A head tracking source that can send the OpenTrack UDP protocol: [OpenTrack](https://github.com/opentrack/opentrack) with a webcam or a VR headset, or a phone app that sends it directly.
- Windows 10 or 11, 64-bit.

Startup discovers and validates the camera addresses and engine layout. A game update can work without a new mod build when those checks pass. If discovery cannot establish a safe layout and no exact historical profile applies, head tracking stays off and `HeadTracking.log` names the failed check. Historical profiles cover Steam build 2484, the GOG build of August 5, 2024, and Xbox Game Pass package 1.0.15.0.

## Installation

### Lopari

Download [Lopari](https://lopari.app), choose **Trepang2**, and click
**Play with head tracking**.

### Standalone Installer

1. Download the installer ZIP from the [Releases](https://github.com/itsloopyo/trepang2-headtracking/releases) page.
2. Extract it anywhere.
3. Double-click `install.cmd`.
4. Configure OpenTrack to output UDP to `127.0.0.1:4242`.
5. Launch the game.

The installer sets up one copy per run. It checks `TREPANG2_PATH`, then Steam, then GOG, then the Xbox app, and installs into the first copy it finds, so on a machine with more than one it takes the first in that order. Point it at a folder to install into that copy instead:

```powershell
# Environment variable
$env:TREPANG2_PATH = "D:\SteamLibrary\steamapps\common\Trepang2"
.\install.cmd

# Or as an argument
.\install.cmd "D:\SteamLibrary\steamapps\common\Trepang2"
```

If you have the game on more than one store, run it again for each other copy with its folder as the argument.

### Manual Installation

The payload goes next to the game's shipping executable:

- Steam and GOG: `CPPFPS\Binaries\Win64\` under the game folder, beside `CPPFPS-Win64-Shipping.exe`.
- Xbox Game Pass: `Content\CPPFPS\Binaries\WinGDK\` under the folder the Xbox app installed the game to, beside `CPPFPS-WinGDK-Shipping.exe`.

No mod manager deploys this mod: they place files into one fixed subtree per game, and this one has to land beside the exe, which is why there is a single installer ZIP and no Nexus page.

From the extracted ZIP:

1. Copy `vendor\ultimate-asi-loader\dinput8.dll` into that folder and rename it to `winmm.dll`. That is an import both shipping executables already have, so it is the proxy name the loader has to use here.
2. Copy `plugins\Trepang2HeadTracking.asi` into the same folder.

`CameraUnlock.ini` and `HeadTracking.log` are written to that folder on first launch.

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

Both columns do the same thing where both are listed. Use whichever your keyboard has. The keys are set in `[Hotkeys]` in `CameraUnlock.ini` (see Configuration), where each action lists every key that fires it.

| Action              | Nav-cluster | Chord          |
|---------------------|-------------|----------------|
| Toggle tracking     | `End`       | `Ctrl+Shift+Y` |
| Cycle tracking mode | `Page Up`   | `Ctrl+Shift+J` |
| Toggle yaw mode     | `Page Down` |                |

Trepang2 runs its own key bindings whether or not Ctrl and Shift are held. It binds `G` to throwing a grenade and `H` to dual wielding by default, so this mod does not use the `Ctrl+Shift+G` and `Ctrl+Shift+H` chords other head tracking mods use: the mode cycle takes `Ctrl+Shift+J`, and yaw mode has no chord. `CycleTrackingModeKey` and `YawModeKey` in `CameraUnlock.ini` hold these keys for this game and do not follow `Defaults.ini`. Whatever you have bound to `Y` or `J` in the game also happens while you press that chord; to use other keys, change the lists in `CameraUnlock.ini`.

One press fires one action. If two actions' lists name a key that a single press would fire both of (`End` in two lists, or `End` in one and `Ctrl+End` in another), the key stays with the action listed first in `[Hotkeys]`, and the log's `hotkey:` line names the one it was left out of. That includes a key a list takes from `Defaults.ini`.

**Toggle tracking** turns head tracking on or off for this session only. Whether it is on when the game starts is `EnableOnStartup`.

**Cycle tracking mode** steps through rotation and position, then rotation only, then position only. The mode is saved to `CameraUnlock.ini` as you change it, and the game starts in it next time.

**Toggle yaw mode** switches which axis head yaw turns about. Horizon-locked is the default: yaw goes about the world up-axis, so looking at the floor and turning your head pans across it. Camera-local turns about the camera's own up-axis instead, which leans the horizon when the camera is pitched steeply. The choice is saved to `CameraUnlock.ini` as you change it, and the game starts in it next time.

### Aiming down sights

Head tracking stays on while you aim. The weapon stays where your mouse or controller points it, so with your head turned it sits off to one side with its sights still lined up, and your rounds land where those sights point. Head movement is scaled to the zoom, so a scope does not magnify it.

Leaning eases out while the sights are up, because it would move your eye off them.

## Configuration

<!-- cameraunlock:config -->
The mod reads its settings from `CameraUnlock.ini` in the game folder, at one of these paths depending on the store the game came from:

- `CPPFPS\Binaries\Win64\CameraUnlock.ini`
- `CPPFPS\Binaries\WinGDK\CameraUnlock.ini`

It creates the file when it starts and finds none. Edit it with any text editor.

A setting set to `default` takes its value from `Defaults.ini`, which every head tracking mod that keeps its settings in `CameraUnlock.ini` reads. Head tracking mods that keep their settings in another file do not read it. Writing a value in place of `default` changes that setting for this game only. When the mod saves a setting that a hotkey changed in game, it writes the new value in place of `default`, so that setting no longer follows `Defaults.ini` in this game until you set it to `default` again.

`Defaults.ini` is `%AppData%\CameraUnlock\Defaults.ini` on Windows; `$XDG_CONFIG_HOME/CameraUnlock/Defaults.ini` on Linux, or `~/.config/CameraUnlock/Defaults.ini` where `XDG_CONFIG_HOME` is not set, under Wine and Proton too; and `~/Library/Application Support/CameraUnlock/Defaults.ini` on macOS. The mod's log, where it writes one, names the file it read.

When the mod starts and finds no `Defaults.ini`, it creates one holding the built-in values, unless Windows runs the game as a packaged app. The mod never changes `Defaults.ini` after that. Edit it with any text editor.

The built-in value of each setting set to `default` below:

- `UdpPort=4242`
- `EnableOnStartup=true`
- `WorldSpaceYaw=true`
- `RotationEnabled=true`
- `LocalSmoothing=0.0`
- `RemoteSmoothing=0.15`
- `PositionEnabled=true`
- `CollisionEnabled=true`
- `CollisionReleaseSmoothing=0.9`
- `ToggleKey=End, Ctrl+Shift+Y`
- `LightFollowsHead=true`
- `LightMultiplier=1.5`

With every setting at its default, the file reads:

```ini
; Trepang2 head tracking settings.
; Comments start with ; and go on their own line. Text after a value is part of the value.
; Hotkeys are key names such as End, PageUp or Ctrl+Shift+Y. Separate several with commas; leave empty for none.
; A setting set to default takes its value from Defaults.ini, which every head tracking mod
; that keeps its settings in CameraUnlock.ini reads: %AppData%\CameraUnlock\Defaults.ini on
; Windows, $XDG_CONFIG_HOME/CameraUnlock/Defaults.ini (normally ~/.config/CameraUnlock) on
; Linux, under Wine and Proton too, and ~/Library/Application Support/CameraUnlock/Defaults.ini
; on macOS. The log names the file it read. Write a value instead of default to change that
; setting for this game only.

[CameraUnlock]
; Written by the mod. Leave this section in place.
ConfigFormat=1

[Network]
; UDP port the mod receives tracker data on (OpenTrack protocol).
UdpPort=default

[General]
; true: head tracking is on when the game starts. ToggleKey turns it on and off.
EnableOnStartup=default
; true: yaw turns around the world's up axis. false: around the camera's own up axis.
WorldSpaceYaw=default
; true: turning your head turns the view.
; Tracking mode at startup, with PositionEnabled. The mode hotkey changes both.
RotationEnabled=default

[Smoothing]
; Smoothing when the tracker runs on this PC. 0 is the least, 1 the most.
LocalSmoothing=default
; Smoothing when the tracker is another device on the network, such as a phone.
; 0 is the least, 1 the most.
RemoteSmoothing=default

[Position]
; true: moving your head moves the view.
; Tracking mode at startup, with RotationEnabled. The mode hotkey changes both.
PositionEnabled=default
; true: leaning stops at walls instead of moving the view through them.
CollisionEnabled=default
; How far the view is held off a wall when you lean into it, in centimetres.
; Keep it above 3, the game's near clip distance.
CollisionMargin=10.0
; Which of the game's collision channels the wall check tests against.
; CollisionChannel=0
; How gently the view eases back out after a wall stopped a lean.
; 0 is the quickest, 1 the slowest.
CollisionReleaseSmoothing=default

[Hotkeys]
; Turns head tracking on and off.
ToggleKey=default
; Changes the tracking mode: rotation and position, rotation only, position only.
CycleTrackingModeKey=PageUp, Ctrl+Shift+J
; Switches yaw between the world's up axis and the camera's own (WorldSpaceYaw).
YawModeKey=PageDown

[Light]
; true: a light you carry points where you look instead of where you aim.
LightFollowsHead=default
; How far the light turns for each degree your head turns.
; 1 matches the view, 0 keeps the light on your aim.
LightMultiplier=default

[Aim]
; Which of the game's collision channels the aim trace tests against, 0 to 31. The
; trace finds where the shot lands, so the crosshair can sit on that point.
; AimTraceChannel=0

[Dev]
; For development. true: run the commands in HeadTracking.devcmd beside the game's
; executable.
DevCommands=false
```
<!-- /cameraunlock:config -->

### Field of view

Trepang2 has its own field of view slider in the video options, running from 70 to 130. The mod reads it while the game runs, so a change to it takes effect on the next frame without a restart.

Head tracking moves the picture by the same amount whatever field of view the game is drawing at. Raising the sights and looking through an optic both narrow it, and a narrower field of view magnifies everything in the frame, head movement included. The mod scales the head pose by the ratio between the field of view being drawn and the one your slider is set to, so a given head movement moves the view as far through a scope as it does walking around. Head roll is left alone, because a tilt of the picture is the same tilt at any field of view. The `fov:` line in the log carries both values and the factor between them, and reads `factor 1.0000` in ordinary play.

### The torch

The torch points along your aim, so without the mod head tracking turns the view away from the beam and leaves the light behind. `LightFollowsHead` takes it off the aim and turns it with your head instead.

It turns further than the view does - `LightMultiplier` times as far, 1.5 by default. When you turn your head you keep your eyes on the thing you turned towards, so what you are actually looking at sits past the middle of the screen, and a beam that only matched the view would land short of it. Set it to `1.0` to have the beam sit where the view is pointed, or to `0` to leave the torch on the aim, which is what the game does unmodded. A value outside 0 to 5 is refused with a line in the log and the default stands.

Nothing else about the torch changes: only which way it points is the mod's, and its brightness and its cone stay the game's. Aiming decides where the rounds go, and the beam moving does not touch that. Whenever tracking is not being applied - a menu, a loading screen, a cutscene, or tracking toggled off with `End` - the beam goes back on the game's own aim. The `torch=` field in the log's heartbeat line says which of those it is doing.

### Window placement

A windowed game is moved once to the centre of the desktop work area on the monitor it opened on, after its window has stopped moving. That is the screen minus the taskbar, so the picture sits a little above the middle of the glass. A fullscreen or borderless window is left where it is, and so is one the game already put there - which is where an ordinary windowed launch lands, so most launches are not moved at all. Nothing is moved when startup cannot resolve the game addresses. The `window:` line in the log says which of those happened.

## Troubleshooting

`HeadTracking.log`, beside the game exe, records the build fingerprint, address discovery, whether the hook installed, the tracker link, and every change in whether head tracking is allowed and why. Read it first.

**Mod not loading:**

- Check that `winmm.dll` and `Trepang2HeadTracking.asi` are both beside the shipping exe (see Manual Installation for the folder on each store). A copy in the game's root folder is never loaded.
- No `HeadTracking.log` at all means the loader never ran; run `install.cmd` again and let it resolve the path itself.
- A `discovery:` failure names an address or layout check that did not pass. Attach the log when reporting head tracking that stays off after a game update.

**No tracking response:**

- You are in a menu, the pause menu, a cutscene, the game's Kamera camera mode, dead, or loading. By design the view is left alone in all of those, and the log names which one.
- Something else has the tracker port. `link: UDP 4242 waiting-for-port` is the mod waiting for it, and the `udp: Failed to bind UDP port 4242` line above it carries the reason Windows gave. Error 10048 is another program already on the port, usually a game left running - close it and the mod takes the port on its next retry, under a second later, without you restarting anything.
- `link: UDP 4242 listening` with no `receiving` line after it means nothing is sending to the port. Check the tracker is running and pointed at this machine on `UdpPort` in `CameraUnlock.ini`.
- Check tracking is not switched off with `End` or `Ctrl+Shift+Y`.

**Jittery or unstable tracking:**

- A phone sending a raw feed direct is the usual cause. Route it through OpenTrack and let its filters clean it up, or raise `RemoteSmoothing`.
- On a webcam, poor or uneven lighting makes the neuralnet tracker's output noisy. Light your face evenly and avoid a bright window behind you.

**Yaw feels wrong when looking up or down at extreme angles:**

- Press `Page Down` to switch yaw mode. Horizon-locked, the default, turns head yaw about the world's up axis, so the horizon stays level however steeply the camera is pitched. Camera-local turns it about the camera's own up axis instead, which tilts the horizon as you turn while looking up or down. Press it again to go back.

**Leaning into a wall stops short:**

- By design. The lean is cut to the room the level leaves, holding the view `CollisionMargin` centimetres off the surface, and the log writes `lean-clamp: holding the view off geometry` when it does. `CollisionEnabled=false` turns that off, at the cost of seeing through walls when you lean into them.

**The crosshair moves when I turn my head:**

- By design. It moves to stay on the point your rounds will land, which is no longer the middle of the screen once your head is turned or leaning. The stealth dot, the kill marker and the tactical visor crosshair move with it. Bullet spread still scatters shots around that point the way it does around the middle of the screen without the mod.

**The weapon is off to one side when I aim down sights.**

- Your head is turned: the weapon stays on your aim and you are looking past it. Turn back to it, or move your aim to where you are looking.

**Known limitations:**

- Only the Steam, GOG and Xbox Game Pass builds named under Requirements have been tested.
- Trepang2 has no multiplayer mode, so the mod covers single player alone.

## Updating

Download the new release and run `install.cmd` again. `CameraUnlock.ini` is left alone, so your settings carry over. Updating from a version that kept its settings in `HeadTracking.ini` imports them into `CameraUnlock.ini` at the first start (see Configuration).

## Uninstalling

Run `uninstall.cmd`. This removes the mod files and its logs, and leaves `CameraUnlock.ini` and `HeadTracking.ini` in place, so your settings survive a reinstall. The loader is only removed if the installer put it there; `uninstall.cmd /force` removes it anyway.

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

- Discord: [Loop's Head Tracking Hangout](https://discord.com/invite/dxyZdyFNT9) - setup help, bug reports, and new-release announcements
- [Lopari](https://lopari.app) - free Windows launcher with one-click install and launch for the released head-tracking mods
- [Headcam](https://headcam.app) - free app that turns your iPhone or Android phone into the head tracker

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
