# Changelog

## [Unreleased]

### Added

- Camera addresses are discovered at startup, allowing game updates with a compatible layout to work without a new mod build. Tracking waits for engine layout validation before applying a pose.
- The tracking mode (`PageUp` / `Ctrl+Shift+J`) and the yaw mode (`PageDown`) are saved to `CameraUnlock.ini` the moment you change them, and the game starts in them next time. `End` still turns head tracking on or off for the current session only; the new `[General] EnableOnStartup` (default `true`) says whether it is on when the game starts.
- A setting set to `default` in `CameraUnlock.ini` takes its value from `Defaults.ini`, which every head tracking mod that keeps its settings in `CameraUnlock.ini` reads. Head tracking mods that keep their settings in another file do not read it, and neither do earlier versions of this mod. Writing a value in place of `default` changes that setting for this game only. When the mod saves a setting that a hotkey changed in game, it writes the new value in place of `default`, so that setting no longer follows `Defaults.ini` in this game until you set it to `default` again.
- `Defaults.ini` is `%AppData%\CameraUnlock\Defaults.ini` on Windows; `$XDG_CONFIG_HOME/CameraUnlock/Defaults.ini` on Linux, or `~/.config/CameraUnlock/Defaults.ini` where `XDG_CONFIG_HOME` is not set, under Wine and Proton too; and `~/Library/Application Support/CameraUnlock/Defaults.ini` on macOS. The mod's log, where it writes one, names the file it read.
- When the mod starts and finds no `Defaults.ini`, it creates one holding the built-in values, unless Windows runs the game as a packaged app. The mod never changes `Defaults.ini` after that.

### Changed
- Settings move to `CPPFPS\Binaries\Win64\CameraUnlock.ini` (Steam and GOG) or `CPPFPS\Binaries\WinGDK\CameraUnlock.ini` (Xbox Game Pass). Earlier versions of the mod kept these settings in `HeadTracking.ini`, in the same folder. The first time this version starts and finds no `CameraUnlock.ini`, it reads your settings from `HeadTracking.ini` and writes them into `CameraUnlock.ini`. It never changes `HeadTracking.ini`, and does not read it again while `CameraUnlock.ini` exists.
- A setting that the defaults the README shows set to `default` is written as `default` when you never changed it from the default earlier versions used, because `HeadTracking.ini` does not hold it or holds that default. It then follows `Defaults.ini`, so it takes the value `Defaults.ini` gives it, or the built-in value where `Defaults.ini` gives none, which can differ from the default earlier versions used. A setting you changed is written with the value imported for it, or as `default` where that value equals its default at that start.
- `RotationEnabled` and `PositionEnabled` are one setting here, the tracking mode, so both are written as `default` or neither is.
- Comments, and keys the mod never read, are not carried over.
- An older version of the mod reads `HeadTracking.ini` and never reads `CameraUnlock.ini`, so a setting you change after updating is not in `HeadTracking.ini`.
- Deleting only `CameraUnlock.ini` makes the next start read `HeadTracking.ini` again. To go back to the defaults, replace everything in `CameraUnlock.ini` with the defaults the README shows. Every setting they set to `default` then follows `Defaults.ini`.
- Hotkeys are written as key names, and each hotkey lists every key that triggers it, the Ctrl+Shift chord included: `ToggleKey=End, Ctrl+Shift+Y`. `End`, `PageUp` and both chords were fixed before and can now be changed or removed like any other key. A `[Hotkeys] YawMode` key becomes a one-key `YawModeKey`, the default `0x22` becoming `YawModeKey=PageDown` and `0x2E` becoming `YawModeKey=Delete`.
- One press still fires one action. A key that two hotkeys would both fire on (`End` in two lists, or `End` in one and `Ctrl+End` in another) stays with the hotkey listed first in `[Hotkeys]`, and the log names the one it was left out of. Before, only a `YawMode` key that was `End` or `PageUp` could clash, and it was refused the same way.
- Settings keep their values under their new names: `[Network] Port` is `UdpPort`, `[Tracking] LocalSmoothing` and `RemoteSmoothing` move to `[Smoothing]`, `[Camera] CollisionEnabled`, `CollisionMargin`, `CollisionChannel` and `CollisionReleaseSmoothing` move to `[Position]`, and `[Camera] AimTraceChannel` moves to `[Aim]`. On and off settings are written `true` and `false`.
- A value in `CameraUnlock.ini` outside a setting's range is not used: it keeps the default, and `HeadTracking.log` names the line. `UdpPort` takes 1 to 65535, the smoothing values 0 to 1, `CollisionMargin` 0 and up and `LightMultiplier` 0 to 5. A `CollisionChannel` outside 0 to 31 turns the lean clamp off for the session, with a line in the log. A value `HeadTracking.ini` held is imported as the earlier versions read it.
- `uninstall.cmd` leaves `CameraUnlock.ini` and `HeadTracking.ini` in place, so your settings survive a reinstall. It deleted `HeadTracking.ini` before.
- The tracking mode and yaw mode hotkeys keep the keys earlier versions used: `CycleTrackingModeKey` is `PageUp, Ctrl+Shift+J` and `YawModeKey` is `PageDown`. Other mods in this series use `Ctrl+Shift+G` and `Ctrl+Shift+H`, but Trepang2 runs its own bindings while Ctrl and Shift are held and binds `G` to throwing a grenade and `H` to dual wielding, so in this mod both rows keep these keys and do not follow `Defaults.ini`.

### Removed
- The aim-down-sights mode cycle on `Insert` / `Ctrl+Shift+U` (faa4054). `[View] AdsMode` and `[Hotkeys] AdsMode` are no longer read: head tracking stays on through the aim whatever the file says.

## [0.2.0] - 2026-09-18

### Added

- support the GOG build of Trepang2

## [0.1.0] - 2026-09-18

### Other

- Hello world

## [0.0.0] - 2026-09-16

### Added

- Added head tracking for Trepang2 on Steam and Xbox / Game Pass: head rotation
  and lean move the first person view while the mouse or controller keeps
  aiming.
- Added crosshair compensation, so the game's crosshair, stealth dot, kill
  marker and tactical visor crosshair sit on the point the shot lands, following
  head rotation and lean. The marks the game draws with the crosshair follow it
  too: the weapon, grenade and interaction prompts, the ability keys and their
  charge rings, and the out-of-ammo text.
- Added a torch that follows the head, so the beam lights what you turned to
  look at rather than what you are aiming at. It turns 1.5 times as far as the
  view, which is where your eyes are once you have turned your head;
  `LightMultiplier` sets that, and `LightFollowsHead` turns the whole thing off.
- Added three aim-down-sights modes (paused, marker, tracked) on `Insert` or
  `Ctrl+Shift+U`.
- Added gameplay gating, so tracking pauses in menus, the pause screen,
  cutscenes, the Kamera camera mode, death and loading.
- Added a lean clamp that holds the view off walls, so leaning does not put the
  eye inside level geometry.
- Added centring of a windowed game on the monitor it opens on, once the game
  has finished placing its window. A fullscreen or borderless window, and one
  the game already centred, are left alone.
