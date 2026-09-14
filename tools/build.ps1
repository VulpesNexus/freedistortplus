<#
.SYNOPSIS
    Builds FreeDistortPlus.aip against a local copy of the Adobe
    Illustrator SDK.

.DESCRIPTION
    The SDK is not redistributable and is not vendored into this repository.
    Point -SdkRoot at your copy, or set the AI_SDK_ROOT environment variable.
#>
[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug')]
    [string] $Configuration = 'Release',
    [string] $SdkRoot = $env:AI_SDK_ROOT
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ai.ps1')

$repo = Split-Path -Parent $PSScriptRoot
$project = Join-Path $repo 'plugin\FreeDistortPlus.vcxproj'
if (-not $SdkRoot) {
    throw 'Set the AI_SDK_ROOT environment variable, or pass -SdkRoot, to point at your copy of the Adobe Illustrator 2026 SDK.'
}
if (-not (Test-Path (Join-Path $SdkRoot 'illustratorapi\illustrator\AILiveEffect.h'))) {
    throw "That does not look like an Illustrator SDK: $SdkRoot"
}

$toolchain = Get-VcToolchain
if (-not (Test-Path $toolchain.MSBuild)) { throw 'MSBuild.exe not found.' }

$output = & $toolchain.MSBuild $project "/p:Configuration=$Configuration" '/p:Platform=x64' '/v:minimal' '/nologo' '/nodeReuse:false' "/p:AISDKRoot=$SdkRoot" 2>&1
$output | ForEach-Object { Hide-Personal ([string] $_) }
if ($LASTEXITCODE -ne 0) { throw "Build failed with exit code $LASTEXITCODE." }

$aip = Join-Path $repo "build\$Configuration\FreeDistortPlus.aip"
if (-not (Test-Path $aip)) { throw "Build reported success but the plugin is missing." }
Write-Output (Hide-Personal $aip)
