# Changelog

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
