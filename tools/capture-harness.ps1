<#
.SYNOPSIS
    Builds the About window and the corners dialog outside Illustrator and
    captures each, in Illustrator's darkest colors, to docs/evidence.

.DESCRIPTION
    Each harness compiles the plugin's own source file unmodified, in a fresh
    ASCII temp folder (the compiler must not see the workspace path's non-ASCII
    characters). The window is captured with PrintWindow, so nothing takes the
    mouse or keyboard, but each window does appear on the desktop for a few
    seconds. Writes docs/evidence/about-dark.png and corner-dialog-dark.png.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ai.ps1')
$repo = Split-Path -Parent $PSScriptRoot
$evidence = Join-Path $repo 'docs\evidence'

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class HarnessCapture {
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
}
"@
[HarnessCapture]::SetProcessDPIAware() | Out-Null

function New-Work {
    $work = Join-Path ([IO.Path]::GetTempPath()) ('fdp-harness-' + [Guid]::NewGuid().ToString('N').Substring(0, 8))
    $null = New-Item -ItemType Directory -Force -Path $work
    $work
}

function Invoke-Compiler([string] $work, [string[]] $rcArgs, [string[]] $clArgs) {
    $toolchain = Get-VcToolchain
    $env:INCLUDE = $toolchain.Include
    $env:LIB = $toolchain.Lib
    $rc = Get-ChildItem (Split-Path -Parent $toolchain.Include.Split(';')[1].Replace('\Include\', '\bin\').Replace('\ucrt', '')) -Recurse -Filter rc.exe -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match '\\x64\\' } | Select-Object -First 1
    Push-Location $work
    try {
        $out = & $rc.FullName @rcArgs 2>&1
        if ($LASTEXITCODE -ne 0) { $out | ForEach-Object { Hide-Personal ([string] $_) }; throw 'The resource compiler failed.' }
        $out = & $toolchain.Cl @clArgs 2>&1
        if ($LASTEXITCODE -ne 0) { $out | ForEach-Object { Hide-Personal ([string] $_) }; throw 'The harness did not compile.' }
    }
    finally { Pop-Location }
}

function Save-WindowCapture([string] $exe, [string] $arguments, [string] $png) {
    $p = Start-Process -FilePath $exe -ArgumentList $arguments -PassThru
    try {
        $deadline = (Get-Date).AddSeconds(15)
        do { Start-Sleep -Milliseconds 300; $p.Refresh() } while (($p.MainWindowHandle -eq [IntPtr]::Zero -or -not [HarnessCapture]::IsWindowVisible($p.MainWindowHandle)) -and -not $p.HasExited -and (Get-Date) -lt $deadline)
        if ($p.MainWindowHandle -eq [IntPtr]::Zero) { throw "No window from $(Split-Path -Leaf $exe)." }
        # Let the first paint, and the dark title bar, land before asking for a copy.
        Start-Sleep -Milliseconds 800
        $r = New-Object HarnessCapture+RECT
        [HarnessCapture]::GetWindowRect($p.MainWindowHandle, [ref] $r) | Out-Null
        $bmp = New-Object Drawing.Bitmap ($r.R - $r.L), ($r.B - $r.T)
        $g = [Drawing.Graphics]::FromImage($bmp)
        $hdc = $g.GetHdc()
        $ok = [HarnessCapture]::PrintWindow($p.MainWindowHandle, $hdc, 2)
        $g.ReleaseHdc($hdc); $g.Dispose()
        if (-not $ok) { $bmp.Dispose(); throw 'PrintWindow failed.' }
        $bmp.Save($png, [Drawing.Imaging.ImageFormat]::Png)
        $bmp.Dispose()
        Write-Output ("{0}: {1}x{2}" -f (Split-Path -Leaf $png), ($r.R - $r.L), ($r.B - $r.T))
    }
    finally {
        if (-not $p.HasExited) { $p.WaitForExit(10000) | Out-Null }
        if (-not $p.HasExited) { $p.Kill() }
    }
}

# --- the About window ------------------------------------------------------
$work = New-Work
foreach ($f in 'plugin\Source\FDPAbout.cpp', 'plugin\Source\FDPAbout.h', 'plugin\Source\FDPID.h', 'plugin\Source\DialogPlacement.h',
               'plugin\Resources\Win\Resource.h', 'plugin\Resources\Win\FreeDistortPlusAbout.rc',
               'tools\AboutHarness\main.cpp', 'tools\AboutHarness\harness.rc', 'tools\AboutHarness\harness.manifest') {
    [IO.File]::Copy((Join-Path $repo $f), (Join-Path $work (Split-Path -Leaf $f)))
}
Invoke-Compiler $work @('/nologo', '/fo', 'harness.res', 'harness.rc') @('/nologo', '/EHsc', '/W4', '/std:c++17', '/DUNICODE', '/D_UNICODE', '/DWIN_ENV', 'main.cpp', 'FDPAbout.cpp', 'harness.res', '/Fe:AboutHarness.exe', '/link', '/SUBSYSTEM:WINDOWS', 'user32.lib', 'gdi32.lib', 'comctl32.lib', 'shell32.lib')
Save-WindowCapture (Join-Path $work 'AboutHarness.exe') '/dark /exit4000' (Join-Path $evidence 'about-dark.png')
[IO.Directory]::Delete($work, $true)

# --- the corners dialog ------------------------------------------------------
$work = New-Work
foreach ($f in 'plugin\Source\CornerDialog.cpp', 'plugin\Source\CornerDialog.h', 'plugin\Source\QuadMath.h', 'plugin\Source\DialogPlacement.h',
               'tools\CornerHarness\main.cpp', 'tools\AboutHarness\harness.manifest') {
    [IO.File]::Copy((Join-Path $repo $f), (Join-Path $work (Split-Path -Leaf $f)))
}
[IO.File]::WriteAllText((Join-Path $work 'harness.rc'), "1 24 `"harness.manifest`"`r`n", (New-Object Text.ASCIIEncoding))
Invoke-Compiler $work @('/nologo', '/fo', 'harness.res', 'harness.rc') @('/nologo', '/EHsc', '/W4', '/WX', '/std:c++17', '/DUNICODE', '/D_UNICODE', '/DWIN_ENV', 'main.cpp', 'CornerDialog.cpp', 'harness.res', '/Fe:CornerHarness.exe', '/link', '/SUBSYSTEM:WINDOWS', 'user32.lib', 'gdi32.lib', 'comctl32.lib')
Save-WindowCapture (Join-Path $work 'CornerHarness.exe') '/dark /exit4000' (Join-Path $evidence 'corner-dialog-dark.png')
[IO.Directory]::Delete($work, $true)
