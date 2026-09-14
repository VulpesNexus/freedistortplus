<#
.SYNOPSIS
    Measures what Adobe Free Distort does with a source quad that is not an
    axis-aligned rectangle, and what Adobe's own edit path makes of one.

.DESCRIPTION
    Adobe's dialog only ever writes a rectangular source. This probe writes
    non-rectangular sources into the dictionary key by key, then for each case:

      1. expands a copy of the fixture and records every anchor and handle
         before and after (stage "stored");
      2. lets Adobe's own edit path commit the effect with alerts off, exactly
         as OK on an untouched dialog does, and records the dictionary it
         wrote and the drawing again (stage "committed").

    The cases are designed ones (each source key alone, pairs of keys, each
    destination key alone, a destination corner moved onto another corner's
    place), shapes (parallelogram, trapezoid, convex, concave, bow-tie, far
    away), seeded random ones, and the same questions over different input
    bounds and over curves with independent handles.

    tools/solve-source-quads.py fits candidate readings to what this records.
    Needs Illustrator running with EnhancedFreeDistort.aip loaded. Writes
    docs/evidence/source-quads-cases.tsv and source-quads-points.tsv.
#>
[CmdletBinding()]
param([string[]] $Only = @())

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ai.ps1')
$repo = Split-Path -Parent $PSScriptRoot
$evidence = Join-Path $repo 'docs\evidence'
$inv = [Globalization.CultureInfo]::InvariantCulture

# Quads are in Adobe's corner order: top-left, top-right, bottom-left,
# bottom-right, h then v. The grid fixture's bounds are 100..400 x 100..300.
$identity = @(100, 300, 400, 300, 100, 100, 400, 100)
$general = @(130, 350, 450, 290, 70, 80, 370, 130)
$convex = @(90, 320, 420, 280, 130, 90, 380, 120)
$trapezoid = @(160, 300, 340, 300, 100, 100, 400, 100)

$cases = New-Object Collections.Generic.List[object]
function Case([string] $name, [string] $fixture, [double[]] $src, [double[]] $dst) {
    $cases.Add(@($name, $fixture, (($src | ForEach-Object { $_.ToString('R', $inv) }) -join ','), (($dst | ForEach-Object { $_.ToString('R', $inv) }) -join ',')))
}
function Plus([double[]] $q, [int] $k, [double] $d) { $n = [double[]] $q.Clone(); $n[$k] += $d; return $n }

$grid = "EFD.grid('fx')"
Case 'rect-bounds-general' $grid $identity $general
Case 'rect-inset-general' $grid @(120, 280, 380, 280, 120, 110, 380, 110) $general
for ($k = 0; $k -lt 8; $k++) {
    foreach ($delta in 37, -37) { Case ("src-key{0}{1:+0;-0}" -f $k, $delta) $grid (Plus $identity $k $delta) $identity }
}
for ($a = 0; $a -lt 8; $a++) {
    for ($b = $a + 1; $b -lt 8; $b++) { Case ("src-keys{0}{1}" -f $a, $b) $grid (Plus (Plus $identity $a 37) $b 23) $identity }
}
foreach ($shape in @(@('convex', $convex), @('trapezoid', $trapezoid))) {
    $sn, $sq = $shape
    Case "$sn-identity" $grid $sq $identity
    Case "$sn-general" $grid $sq $general
    for ($k = 0; $k -lt 8; $k++) { Case ("{0}-dst-key{1}+37" -f $sn, $k) $grid $sq (Plus $identity $k 37) }
    for ($i = 0; $i -lt 4; $i++) {
        $d = [double[]] $identity.Clone(); $d[2 * $i] += 37; $d[2 * $i + 1] += 37
        Case ("{0}-dst-corner{1}-diagonal" -f $sn, $i) $grid $sq $d
        $j = 3 - $i
        $d = [double[]] $identity.Clone(); $d[2 * $i] = $identity[2 * $j]; $d[2 * $i + 1] = $identity[2 * $j + 1]
        Case ("{0}-dst-corner{1}-at-corner{2}" -f $sn, $i, $j) $grid $sq $d
    }
}
Case 'parallelogram-general' $grid @(140, 300, 440, 300, 100, 100, 400, 100) $general
Case 'concave-identity' $grid @(100, 300, 400, 300, 100, 100, 250, 220) $identity
Case 'concave-general' $grid @(100, 300, 400, 300, 100, 100, 250, 220) $general
Case 'bow-tie-identity' $grid @(400, 300, 100, 300, 100, 100, 400, 100) $identity
Case 'far-convex-general' $grid @(1090, 1320, 1420, 1280, 1130, 1090, 1380, 1120) $general

$rng = New-Object Random 20260914
function Jitter([double[]] $q, [double] $r) { [double[]] ($q | ForEach-Object { $_ + ($rng.NextDouble() * 2 - 1) * $r }) }
for ($i = 0; $i -lt 16; $i++) { Case ("random-{0:00}" -f $i) $grid (Jitter $identity 80) (Jitter $identity 100) }
for ($i = 0; $i -lt 4; $i++) { Case ("random-wild-{0:00}" -f $i) $grid (Jitter $identity 250) (Jitter $identity 250) }

# Other input bounds: the grid scaled 1.5 x 0.6 with its corner at (250, 150),
# so bounds 250..700 x 150..270.
$wide = "EFD.gridAt('fx', 250, 150, 1.5, 0.6)"
$wideBounds = @(250, 270, 700, 270, 250, 150, 700, 150)
function Offset([double[]] $base, [double[]] $by) { [double[]] (0..7 | ForEach-Object { $base[$_] + $by[$_] }) }
$convexDeviation = [double[]] (0..7 | ForEach-Object { $convex[$_] - $identity[$_] })
$generalDeviation = [double[]] (0..7 | ForEach-Object { $general[$_] - $identity[$_] })
Case 'wide-convex-identity' $wide (Offset $wideBounds $convexDeviation) $wideBounds
Case 'wide-convex-general' $wide (Offset $wideBounds $convexDeviation) (Offset $wideBounds $generalDeviation)
Case 'wide-grid-source-convex' $wide $convex $general
for ($i = 0; $i -lt 6; $i++) { Case ("wide-random-{0:00}" -f $i) $wide (Jitter $wideBounds 90) (Jitter $wideBounds 110) }

# Curves with independent handles; their bounds are not a round rectangle.
$curves = "EFD.curves('fx')"
Case 'curves-convex-general' $curves $convex $general
for ($i = 0; $i -lt 4; $i++) { Case ("curves-random-{0:00}" -f $i) $curves (Jitter $identity 80) (Jitter $identity 100) }

$keys = 'src0h', 'src0v', 'src1h', 'src1v', 'src2h', 'src2v', 'src3h', 'src3v'
# Two files. source-quads-cases.tsv: one row per case, the dictionary as written and
# as Adobe committed it, and whether that commit changed the drawing.
# source-quads-points.tsv: the fixture's anchors and handles once per fixture
# (kind S, case column = fixture), the drawing of each case (kind R), and the
# drawing after Adobe's commit (kind C) only for a case where it differs.
$caseRows = New-Object Collections.Generic.List[string]
$caseRows.Add("case`tfixture`tstored source`tstored destination`tcommitted source`tcommitted destination`tcommit changed the drawing")
$pointRows = New-Object Collections.Generic.List[string]
$pointRows.Add("case`tkind`tindex`tah`tav`tlh`tlv`trh`trv")
$fixturesWritten = @{}
Initialize-AiSession | Out-Null
Send-AiMessage 'tool select' 'Adobe Select Tool' | Out-Null

function Stored {
    $read = Send-AiMessage 'fd read' '0'
    $m = [regex]::Match($read, 'src \(([^)]*)\) dst \(([^)]*)\)')
    if (-not $m.Success) { throw "cannot read the dictionary: $read" }
    return @(($m.Groups[1].Value -replace ' ', ','), ($m.Groups[2].Value -replace ' ', ','))
}
function Drawing {
    $s = New-Object Collections.Generic.List[string]
    $r = New-Object Collections.Generic.List[string]
    foreach ($row in ((Invoke-Efd 'EFD.sourceAndResult("fx");') -split "`r?`n")) {
        if (-not $row) { continue }
        $kind, $rest = $row -split "`t", 2
        if ($kind -eq 'S') { $s.Add($rest) } else { $r.Add($rest) }
    }
    return @(, @($s.ToArray(), $r.ToArray()))
}

$done = 0
foreach ($c in $cases) {
    $name, $fixture, $src, $dst = $c
    if ($Only.Count -and $Only -notcontains $name) { continue }
    Invoke-Efd "EFD.clear(); $fixture; EFD.selectOnly('fx');" | Out-Null
    Send-AiMessage 'fd append' | Out-Null
    $wrote = Send-AiMessage 'fd write' ("0|100,300,400,100|{0}" -f $dst)
    if ($wrote -notmatch "result`t0") { throw "write failed for $name`: $wrote" }
    $s = $src -split ','
    for ($k = 0; $k -lt 8; $k++) {
        $set = Send-AiMessage 'set param' ("0|{0}|real|{1}" -f $keys[$k], $s[$k])
        if ($set -match 'fail|error|Expected') { throw "set $($keys[$k]) failed for $name`: $set" }
    }
    $srcNow, $dstNow = Stored
    $before = Drawing
    Send-AiMessage 'edit effect' '0' | Out-Null
    $srcAdobe, $dstAdobe = Stored
    $after = Drawing

    if (-not $fixturesWritten.ContainsKey($fixture)) {
        foreach ($p in $before[0]) { $pointRows.Add(("{0}`tS`t{1}" -f $fixture, $p)) }
        $fixturesWritten[$fixture] = $true
    }
    foreach ($p in $before[1]) { $pointRows.Add(("{0}`tR`t{1}" -f $name, $p)) }
    $changed = ($before[1] -join "`n") -ne ($after[1] -join "`n")
    if ($changed) { foreach ($p in $after[1]) { $pointRows.Add(("{0}`tC`t{1}" -f $name, $p)) } }
    $caseRows.Add(("{0}`t{1}`t{2}`t{3}`t{4}`t{5}`t{6}" -f $name, $fixture, $srcNow, $dstNow, $srcAdobe, $dstAdobe, $(if ($changed) { 'yes' } else { 'no' })))
    $done++
    if ($done % 20 -eq 0) { Write-Output ("{0} cases" -f $done) }
}
[IO.File]::WriteAllLines((Join-Path $evidence 'source-quads-cases.tsv'), (Hide-Personal $caseRows))
[IO.File]::WriteAllLines((Join-Path $evidence 'source-quads-points.tsv'), (Hide-Personal $pointRows))
Write-Output ("{0} cases written" -f $done)
