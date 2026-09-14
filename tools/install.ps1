<#
.SYNOPSIS
    Copies FreeDistortPlus.aip into Illustrator's Additional Plug-ins
    Folder, or takes it out again.

.DESCRIPTION
    The Additional Plug-ins Folder is one per-user preference shared by every
    plugin, so this touches exactly one file in it and nothing else. It needs
    no administrator rights. Illustrator reads the folder only at startup and
    holds a loaded plugin open, so it refuses to run while Illustrator does.

.EXAMPLE
    .\tools\install.ps1
    .\tools\install.ps1 -Uninstall
#>
[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug')]
    [string] $Configuration = 'Release',
    [string] $PluginFolder,
    [switch] $Uninstall
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ai.ps1')

if (Get-Process Illustrator -ErrorAction SilentlyContinue) {
    throw 'Quit Illustrator first: it holds loaded plugins open and reads the folder only at startup.'
}

if (-not $PluginFolder) { $PluginFolder = Get-AiAdditionalPluginFolder }
if (-not $PluginFolder) {
    throw "Illustrator's Additional Plug-ins Folder is not set. Set it once under Edit > Preferences > Plug-ins & Scratch Disks, or pass -PluginFolder."
}
if (-not (Test-Path $PluginFolder)) { throw 'The Additional Plug-ins Folder does not exist.' }

$target = Join-Path $PluginFolder 'FreeDistortPlus.aip'
if ($Uninstall) {
    if (Test-Path $target) {
        [System.IO.File]::Delete($target)
        Write-Output 'Removed FreeDistortPlus.aip.'
    }
    else { Write-Output 'FreeDistortPlus.aip was not installed.' }
    return
}

$repo = Split-Path -Parent $PSScriptRoot
$source = Join-Path $repo "build\$Configuration\FreeDistortPlus.aip"
if (-not (Test-Path $source)) { throw 'Build the plugin first: tools\build.ps1' }
[System.IO.File]::Copy($source, $target, $true)
$hash = (Get-FileHash $target -Algorithm SHA256).Hash
Write-Output ("Installed FreeDistortPlus.aip ({0}), SHA-256 {1}." -f $Configuration, $hash)
