<#
.SYNOPSIS
    Build, quit Illustrator, install, and start Illustrator again.

.DESCRIPTION
    Illustrator reads its plugin folders only at startup and holds a loaded
    plugin open, so every change to the plugin needs these four moves.
    Quitting closes every open document without saving: on a machine where
    Illustrator is shared with other work, make sure nobody else is using it.
#>
[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug')]
    [string] $Configuration = 'Release',
    [string] $SdkRoot = $env:AI_SDK_ROOT,
    [switch] $SkipBuild
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ai.ps1')

if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot 'build.ps1') -Configuration $Configuration -SdkRoot $SdkRoot | Select-Object -Last 1
}
Stop-Ai
& (Join-Path $PSScriptRoot 'install.ps1') -Configuration $Configuration
Start-Ai
Send-AiMessage version
