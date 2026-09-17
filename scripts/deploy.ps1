# SPDX-License-Identifier: MIT
# Copyright (c) 2026 itsloopyo

<#
.SYNOPSIS
    Deploys the built .asi and the vendored ASI loader into every local install
    of Trepang2 (the dev loop).

.DESCRIPTION
    Unattended: no prompts, exits non-zero with a diagnostic on any failure.
    Every copy Find-AllGamePaths reports is written to; a supplied path wins.
    The mod writes its own default HeadTracking.ini on first launch, so no
    config file is copied.

.PARAMETER GamePath
    Trepang2 install root. Omit to deploy to every detected install.
#>

[CmdletBinding()]
param([Parameter(Position = 0)][string]$GamePath)
$ErrorActionPreference = 'Stop'

$root = Resolve-Path (Join-Path $PSScriptRoot '..')

Import-Module (Join-Path $root 'cameraunlock-core/powershell/DevDeploy.psm1') -Force
Import-Module (Join-Path $root 'cameraunlock-core/powershell/ModDeployment.psm1') -Force

# Both the Steam exe (CPPFPS\Binaries\Win64) and the Xbox / Game Pass exe
# (CPPFPS\Binaries\WinGDK) import WINMM.dll statically, so the loader goes in as
# winmm.dll beside whichever exe the install has, exactly as install.cmd does it.
$null = Invoke-DevDeployASILoader `
    -GameId 'trepang2' `
    -GameDisplayName 'Trepang2' `
    -BuildOutputPath (Join-Path $root 'build/Release') `
    -ModDllName 'Trepang2HeadTracking.asi' `
    -VendorLoaderDll (Join-Path $root 'vendor/ultimate-asi-loader/dinput8.dll') `
    -AsiLoaderName 'winmm.dll' `
    -GivenPath $GamePath
