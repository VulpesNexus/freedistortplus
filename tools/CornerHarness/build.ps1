<#
.SYNOPSIS
    Builds tools/CornerHarness/CornerHarness.exe, the corners dialog on its
    own, and runs its scripted checks.

.DESCRIPTION
    Compiles plugin/Source/CornerDialog.cpp unmodified, in a fresh ASCII temp
    folder (the compiler's working directory must not contain the workspace
    path's non-ASCII characters), with the common-controls version 6 manifest a
    bare executable needs. -Test runs the checks and writes
    docs/evidence/corner-dialog.txt and corner-dialog.tsv.
#>
[CmdletBinding()]
param([switch] $Test)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\ai.ps1')
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$toolchain = Get-VcToolchain
$work = Join-Path ([IO.Path]::GetTempPath()) ('fdp-corners-' + [Guid]::NewGuid().ToString('N').Substring(0, 8))
$null = New-Item -ItemType Directory -Force -Path $work
foreach ($f in 'plugin\Source\CornerDialog.cpp', 'plugin\Source\CornerDialog.h', 'plugin\Source\QuadMath.h', 'plugin\Source\DialogPlacement.h') {
    [IO.File]::Copy((Join-Path $repo $f), (Join-Path $work (Split-Path -Leaf $f)))
}
[IO.File]::Copy((Join-Path $PSScriptRoot 'main.cpp'), (Join-Path $work 'main.cpp'))
[IO.File]::Copy((Join-Path $repo 'tools\AboutHarness\harness.manifest'), (Join-Path $work 'harness.manifest'))
[IO.File]::WriteAllText((Join-Path $work 'harness.rc'), "1 24 `"harness.manifest`"`r`n", (New-Object Text.ASCIIEncoding))

$env:INCLUDE = $toolchain.Include
$env:LIB = $toolchain.Lib
$rc = Get-ChildItem (Split-Path -Parent $toolchain.Include.Split(';')[1].Replace('\Include\', '\bin\').Replace('\ucrt', '')) -Recurse -Filter rc.exe -ErrorAction SilentlyContinue | Where-Object { $_.FullName -match '\\x64\\' } | Select-Object -First 1
Push-Location $work
try {
    & $rc.FullName /nologo /fo harness.res harness.rc | Out-Null
    $out = & $toolchain.Cl /nologo /EHsc /W4 /WX /std:c++17 /DUNICODE /D_UNICODE /DWIN_ENV main.cpp CornerDialog.cpp harness.res /Fe:CornerHarness.exe /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib comctl32.lib 2>&1
    if ($LASTEXITCODE -ne 0) { $out | ForEach-Object { Hide-Personal ([string] $_) }; throw 'The corners harness did not compile.' }
}
finally { Pop-Location }
$exe = Join-Path $work 'CornerHarness.exe'
[IO.File]::Copy($exe, (Join-Path $PSScriptRoot 'CornerHarness.exe'), $true)

if ($Test) {
    $result = Join-Path $work 'result.txt'
    $p = Start-Process -FilePath $exe -ArgumentList "/test:$result" -PassThru -Wait
    $lines = [IO.File]::ReadAllLines($result)
    Start-ProbeResults -Probe 'corner-dialog'
    foreach ($line in $lines) {
        $f = $line -split "`t"
        if ($f.Count -ge 3) { Add-ProbeResult -Group 'dialog' -Case $f[1] -Expected 'passes' -Observed $f[2] -Status $f[0] }
    }
    Save-ProbeResults -Path (Join-Path $repo 'docs\evidence\corner-dialog.tsv')
    Save-ProbeTranscript -Path (Join-Path $repo 'docs\evidence\corner-dialog.txt') -Lines (@('FreeDistort+ -- the corners dialog, driven by window messages in tools/CornerHarness, outside Illustrator', '') + $lines)
    $lines
    if ($p.ExitCode -ne 0) { throw 'A corners dialog check failed.' }
}
[IO.Directory]::Delete($work, $true)
