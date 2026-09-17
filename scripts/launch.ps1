# SPDX-License-Identifier: MIT
# Copyright (c) 2026 itsloopyo

<#
.SYNOPSIS
    Launches Trepang2 for a head-tracking test run.

.DESCRIPTION
    Unattended - Start-Process without -Wait, no prompts.

.PARAMETER Store
    Steam (default) launches through steam.exe. GamePass launches the Xbox /
    Game Pass package by its app id.

.PARAMETER Windowed
    Steam only: launch windowed at -ResX by -ResY instead of the saved display
    mode. The Game Pass build takes its command line from the package's
    UE4CommandLine.txt instead.
#>

[CmdletBinding()]
param(
    [ValidateSet('Steam', 'GamePass')][string]$Store = 'Steam',
    [switch]$Windowed,
    [int]$ResX = 1920,
    [int]$ResY = 1080
)
$ErrorActionPreference = 'Stop'

if ($Store -eq 'GamePass') {
    $package = Get-AppxPackage -Name 'Team17DigitalLimited.Trepang2'
    if (-not $package) {
        Write-Host 'ERROR: the Game Pass package Team17DigitalLimited.Trepang2 is not installed.' -ForegroundColor Red
        exit 1
    }
    $appId = "shell:appsFolder\$($package.PackageFamilyName)!AppTREPANG2Shipping"
    Write-Host "Launching Trepang2 (Game Pass): $appId" -ForegroundColor Cyan
    Start-Process $appId
    exit 0
}

$steam = Join-Path ${env:ProgramFiles(x86)} 'Steam\steam.exe'
if (-not (Test-Path $steam)) {
    Write-Host "ERROR: steam.exe not found at $steam." -ForegroundColor Red
    exit 1
}

$gameArgs = @()
if ($Windowed) { $gameArgs += @('-windowed', "-ResX=$ResX", "-ResY=$ResY") }

Write-Host "Launching Trepang2 via Steam: $($gameArgs -join ' ')" -ForegroundColor Cyan
Start-Process -FilePath $steam -ArgumentList (@('-applaunch', '1164940') + $gameArgs)
