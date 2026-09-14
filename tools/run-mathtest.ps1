<#
.SYNOPSIS
    Compiles and runs the quad arithmetic test, which needs neither
    Illustrator nor the Adobe SDK.

.DESCRIPTION
    plugin/Source/QuadMath.h is compiled unmodified and checked against host
    measurements and against the invariants of every editing mode. Needs only a
    Visual Studio C++ toolchain.
#>
[CmdletBinding()]
param([string] $OutPath)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ai.ps1')

$repo = Split-Path -Parent $PSScriptRoot
$source = Join-Path $PSScriptRoot 'mathtest\mathtest.cpp'
$plugin = Join-Path $repo 'plugin\Source'
# A fresh ASCII directory each run: the compiler's working directory must not
# sit under a path with non-ASCII characters, and a shared directory can leave
# the previous executable locked.
$work = Join-Path ([IO.Path]::GetTempPath()) ('fdp-mathtest-' + [Guid]::NewGuid().ToString('N').Substring(0, 8))
if (-not $OutPath) { $OutPath = Join-Path $repo 'docs\evidence\mathtest.txt' }
$null = New-Item -ItemType Directory -Force -Path $work
$null = New-Item -ItemType Directory -Force -Path (Split-Path -Parent $OutPath)

$toolchain = Get-VcToolchain
$env:INCLUDE = $toolchain.Include
$env:LIB = $toolchain.Lib

$exe = Join-Path $work 'mathtest.exe'
Push-Location $work
try {
    $compile = & $toolchain.Cl /nologo /EHsc /std:c++17 /W4 /WX /O2 "/I$plugin" $source "/Fe:$exe" 2>&1
    $compile | ForEach-Object { Write-Output $_ }
    if ($LASTEXITCODE -ne 0) { throw "The arithmetic test did not compile (exit $LASTEXITCODE)." }
}
finally { Pop-Location }

$output = & $exe 2>&1
$code = $LASTEXITCODE
$output | ForEach-Object { Write-Output $_ }

$lines = New-Object Collections.Generic.List[string]
$lines.Add('FreeDistort+ -- quad arithmetic, without Illustrator')
$lines.Add(("Compiler: cl.exe {0}, /W4 /WX /std:c++17 /O2" -f $toolchain.Version))
$lines.Add('')
foreach ($line in $output) { $lines.Add([string] $line) }
Save-ProbeTranscript -Path $OutPath -Lines $lines

$summary = ($output | Where-Object { $_ -match '^\d+ checks' } | Select-Object -Last 1)
if ($summary -match '^(\d+) checks, (\d+) failed') {
    Start-ProbeResults -Probe 'mathtest'
    Add-ProbeResult -Group 'arithmetic' -Case 'QuadMath.h against host samples and editing invariants' -Expected 'every check passes' -Observed ("{0} checks, {1} failed" -f $Matches[1], $Matches[2]) -Status $(if ([int] $Matches[2] -eq 0) { 'PASS' } else { 'FAIL' })
    # One row per check as well, so the support matrix can name the checks a
    # feature rests on.
    foreach ($line in $output) {
        if ([string] $line -match '^(PASS|FAIL)  (.+?)(  --  (.*))?$') {
            Add-ProbeResult -Group 'check' -Case $Matches[2] -Expected 'passes' -Observed $(if ($Matches[4]) { $Matches[4] } else { $Matches[1].ToLower() }) -Status $Matches[1]
        }
    }
    Save-ProbeResults -Path ($OutPath -replace '\.txt$', '.tsv')
}

[System.IO.Directory]::Delete($work, $true)
if ($code -ne 0) { throw 'The arithmetic test failed.' }
