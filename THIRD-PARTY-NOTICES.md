# Third-Party Notices

Trepang2HeadTracking bundles, statically links, or credits the third-party components
listed below. Each remains the property of its authors and is used under its own
licence. Where a licence requires the copyright notice, the conditions and the
disclaimer to accompany a binary distribution, the full text is reproduced here
verbatim, and this file ships at the root of every release ZIP we publish.

The mod's own code is MIT licensed, copyright itsloopyo; see
`LICENSE` at the root of this repository and of every release ZIP.

This repository contains no Trepang2 game code, no extracted game assets and no
game data files.

---

## Ultimate ASI Loader

- **Version:** `v9.7.4` (commit `6b440669144c4a0bef5718ab155df160d231cd42`)
- **License:** MIT
- **Upstream:** https://github.com/ThirteenAG/Ultimate-ASI-Loader
- **Usage:** Loads `Trepang2HeadTracking.asi` into the game process; installed as `winmm.dll` beside the game exe (`CPPFPS/Binaries/Win64/` on Steam, `CPPFPS/Binaries/WinGDK/` on Xbox / Game Pass), the proxy both shipping exes already import.
- **Bundled:** yes. Bundled in the release ZIP as the install-time source of truth; `install.cmd` extracts the vendored copy and never fetches a loader from the network.

Vendored at `vendor/ultimate-asi-loader/`. Taken from the upstream release asset
untouched; the upstream licence file ships beside it at
`vendor/ultimate-asi-loader/LICENSE`.

- Asset: `Ultimate-ASI-Loader_x64.zip`
- `dinput8.dll` SHA-256: `fa266e3513d02c08a1b808f28c10538a489eaffaa4b0707f7cc1066e71b5afd7`

```
MIT License

Copyright (c) 2023 ThirteenAG

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

That `dinput8.dll` is a static binary and is not one component. The
`Ultimate-ASI-Loader-x64` target in `premake5.lua` at v9.7.4 compiles
`external/injector/minhook/src/**.c`,
`external/injector/utility/FunctionHookMinHook.cpp` and `external/miniz/miniz.c`
alongside the loader's own sources, so redistributing it redistributes MinHook,
injector and miniz as well, and each has its own section in this file. The
`.?AVFunctionHookMinHook@@` RTTI string is present in the vendored binary. The
d3d8to9, MemoryModule and minidx9 files belong to the 32-bit target and are
absent from the x64 project. The MinHook section covers the copy inside the
loader as well as the one linked into the mod itself; the licence text is the
same.

---

## injector

- **Version:** commit `3a384e8d1b575c09383b0fab8bd92e34cb654949`, the submodule
  Ultimate ASI Loader v9.7.4 pins at `external/injector/`
- **License:** Zlib
- **Upstream:** https://github.com/ThirteenAG/injector
- **Usage:** The loader's `FunctionHookMinHook` wrapper, which the
  `Ultimate-ASI-Loader-x64` target compiles from
  `external/injector/utility/FunctionHookMinHook.cpp`, and the MinHook submodule
  that repository carries. Nothing in this repository calls or links it; it
  ships only inside that binary.
- **Bundled:** yes. Compiled into the shipped `dinput8.dll`.

The binary is unaltered upstream, so the "altered source versions" condition
below does not arise. It is reproduced whole regardless.

```
Copyright (C) 2012-2014 LINK/2012 <dma_2012@hotmail.com>

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

   1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.

   2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.

   3. This notice may not be removed or altered from any source
   distribution.
```

---

## miniz

- **Version:** as vendored at `external/miniz/` in Ultimate ASI Loader v9.7.4
- **License:** MIT
- **Upstream:** https://github.com/richgel999/miniz
- **Usage:** Zip reading for the loader, which the `Ultimate-ASI-Loader-x64`
  target compiles from `external/miniz/miniz.c`. Nothing in this repository
  calls or links it; it ships only inside that binary.
- **Bundled:** yes. Compiled into the shipped `dinput8.dll`.

```
Copyright 2013-2014 RAD Game Tools and Valve Software
Copyright 2010-2014 Rich Geldreich and Tenacious Software LLC

All Rights Reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

---

## MinHook

- **Version:** `v1.3.4`, with one local change recorded in `cameraunlock-core/vendor/minhook/LOCAL-CHANGES.md`: MinHook allocates from the process heap rather than standing up a private one. Upstream's own build files still compute `MINHOOK_VERSION` as `1.3.3` on the v1.3.4 tree, so that string appears in the binary.
- **License:** BSD-2-Clause
- **Upstream:** https://github.com/TsudaKageyu/minhook
- **Usage:** Trampoline hooking for the `APlayerController::GetPlayerViewPoint` hook, and for the `UObject::ProcessEvent` hook the developer command channel installs when it is switched on. Built from the copy vendored in cameraunlock-core (`cameraunlock-core/vendor/minhook`) as the `minhook` target that `cameraunlock_hooks` links against, and statically linked.
- **Bundled:** yes, as compiled code inside `Trepang2HeadTracking.asi`. No separate MinHook file ships.

MinHook carries two copyright holders: Tsuda Kageyu for MinHook itself, and
Vyacheslav Patkov for the Hacker Disassembler Engine that `src/hde/` is built
from. Both notices appear below exactly as upstream ships them.

```
MinHook - The Minimalistic API Hooking Library for x64/x86
Copyright (C) 2009-2017 Tsuda Kageyu.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

 1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER
OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

================================================================================
Portions of this software are Copyright (c) 2008-2009, Vyacheslav Patkov.
================================================================================
Hacker Disassembler Engine 32 C
Copyright (c) 2008-2009, Vyacheslav Patkov.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

 1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

-------------------------------------------------------------------------------
Hacker Disassembler Engine 64 C
Copyright (c) 2008-2009, Vyacheslav Patkov.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

 1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

---

## cameraunlock-core

- **Version:** commit `76304a27497c2d5280363a7b0c293cca9ece42aa`
- **License:** MIT
- **Upstream:** https://github.com/itsloopyo/cameraunlock-core
- **Usage:** Shared head-tracking runtime (UDP receiver, pose processing, lean clamp, hook manager, Unreal helpers). Git submodule at `cameraunlock-core/`, statically linked.
- **Bundled:** yes, as compiled code inside `Trepang2HeadTracking.asi`. Its installer bodies and game-detection scripts also ship in the release ZIP's `shared/`, under the same licence.

Our own code, MIT licensed, reproduced here so the notices are complete.

```
MIT License

Copyright (c) 2026 itsloopyo

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

---

## OpenTrack

- **Version:** n/a (wire format only; no released version is consumed)
- **License:** ISC
- **Upstream:** https://github.com/opentrack/opentrack
- **Usage:** This mod consumes the OpenTrack UDP pose datagram layout, on port 4242 by default, so that OpenTrack and compatible trackers can drive it.
- **Bundled:** no. Not bundled and not linked. No OpenTrack code, headers or binaries are copied, linked or redistributed, so its licence triggers no notice obligation here. It is credited because the wire format is its work.

---

## Trepang2

Trepang2 and all related names, logos, characters and marks are trademarks of
their respective owners. They are used here only to identify the game this mod
applies to, which is nominative use and not a claim of any right in them. This
project is an unofficial, fan-made modification. It is not affiliated with,
endorsed by, or sponsored by Trepang Studios, Team17, the game's engine vendor,
or any other rights holder. It redistributes no game code, no extracted game
assets and no proprietary DLLs, and it requires a legitimately purchased copy of
the game. The function addresses and structure offsets in the source are numbers
read from legitimately owned copies of the game's executables and from the
engine's own runtime type information; no game code is stored in this
repository. The reflected class, property and function names the mod looks up at
runtime (for example `BasePlayer::FirstPersonCameraComponent`, `WBCrosshair_C`
and `BaseGameMode_C::GetIsPlayingCutscene`) are the game's own identifiers, used
only to find those objects in the running game.
