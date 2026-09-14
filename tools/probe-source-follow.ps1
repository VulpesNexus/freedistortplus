<#
.SYNOPSIS
    Whether Illustrator's own operations ever give Free Distort a source quad
    that is not a rectangle, and how the source "follows" the art when the art
    is edited after the effect exists.

.DESCRIPTION
    For each operation: a pentagon gets a Free Distort committed by Adobe's own
    edit path and then distorted, the operation is applied to the art, and the
    probe records the dictionary, the drawing, the dictionary Adobe's edit path
    commits afterwards (with alerts off, as OK on an untouched dialog), and
    whether that commit changed the drawing.

    The records use the same two-file layout as probe-source-quads.ps1, so
    tools/solve-source-quads.py checks the drawing after every operation
    against the same reading. The pass/fail facts go to source-follow.txt and
    source-follow.tsv.

    Needs Illustrator running with FreeDistortPlus.aip loaded.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ai.ps1')
$repo = Split-Path -Parent $PSScriptRoot
$evidence = Join-Path $repo 'docs\evidence'
$inv = [Globalization.CultureInfo]::InvariantCulture

$log = New-Object Collections.Generic.List[string]
function Say([string] $s) { $log.Add($s); Write-Output $s }
function Check([string] $group, [string] $case, [string] $expected, [string] $observed, [bool] $ok) {
    $status = if ($ok) { 'PASS' } else { 'FAIL' }
    Add-ProbeResult -Group $group -Case $case -Expected $expected -Observed $observed -Status $status
    Say ("[{0}] {1}: {2}" -f $status, $case, $observed)
}
function Record([string] $group, [string] $case, [string] $observed) {
    Add-ProbeResult -Group $group -Case $case -Observed $observed -Status 'MEASURED'
    Say ("[MEASURED] {0}: {1}" -f $case, $observed)
}
function Quads {
    $read = Send-AiMessage 'fd read' '0'
    $m = [regex]::Match($read, 'src \(([^)]*)\) dst \(([^)]*)\)')
    if (-not $m.Success) { throw "cannot read the dictionary: $read" }
    return @(($m.Groups[1].Value -replace ' ', ','), ($m.Groups[2].Value -replace ' ', ','))
}
function IsRectangle([string] $q) {
    $n = [double[]] ($q -split ',' | ForEach-Object { [double]::Parse($_, $inv) })
    return ($n[1] -eq $n[3] -and $n[5] -eq $n[7] -and $n[0] -eq $n[4] -and $n[2] -eq $n[6] -and $n[2] -gt $n[0] -and $n[1] -gt $n[5])
}
function Drawing([string] $name) {
    $s = New-Object Collections.Generic.List[string]
    $r = New-Object Collections.Generic.List[string]
    foreach ($row in ((Invoke-Fdp ("FDP.sourceAndResult('{0}');" -f $name)) -split "`r?`n")) {
        if (-not $row) { continue }
        $kind, $rest = $row -split "`t", 2
        if ($kind -eq 'S') { $s.Add($rest) } else { $r.Add($rest) }
    }
    return @(, @($s.ToArray(), $r.ToArray()))
}

$caseRows = New-Object Collections.Generic.List[string]
$caseRows.Add("case`tfixture`tstored source`tstored destination`tcommitted source`tcommitted destination`tcommit changed the drawing")
$pointRows = New-Object Collections.Generic.List[string]
$pointRows.Add("case`tkind`tindex`tah`tav`tlh`tlv`trh`trv")

Start-ProbeResults -Probe 'source-follow'
Say ('FreeDistort+ -- how the source follows the art, {0}' -f (Get-Date -Format 'yyyy-MM-dd HH:mm'))
Initialize-AiSession | Out-Null
Send-AiMessage 'tool select' 'Adobe Select Tool' | Out-Null

# A pentagon (bounds 90..340 x 100..330) whose Free Distort Adobe's own edit
# path committed, then distorted in the form Adobe's dialog writes: source =
# the input bounds, destination = the new corners.
function Fresh([string] $name) {
    Invoke-Fdp ("FDP.clear(); FDP.pentagon('{0}'); FDP.selectOnly('{0}');" -f $name) | Out-Null
    Send-AiMessage 'fd append' | Out-Null
    Send-AiMessage 'edit effect' '0' | Out-Null
    Send-AiMessage 'fd write' '0|90,330,340,100|110,350,390,320,70,90,360,120' | Out-Null
}

$operations = @(
    @('move', 'o.translate(50, 20);'),
    @('scale-uniform-effects-scaled', 'o.resize(150, 150, true, true, true, true, 150, Transformation.CENTER);'),
    @('scale-non-uniform', 'o.resize(150, 60, true, true, true, true, 100, Transformation.CENTER);'),
    @('scale-non-uniform-effects-scaled', 'o.resize(150, 60, true, true, true, true, 95, Transformation.CENTER);'),
    @('rotate-30', 'o.rotate(30);'),
    @('rotate-90', 'o.rotate(90);'),
    @('reflect', 'o.resize(-100, 100, true, true, true, true, 100, Transformation.CENTER);'),
    @('shear-20', 'var m = app.getIdentityMatrix(); m.mValueC = Math.tan(20 * Math.PI / 180); o.transform(m, true, true, true, true, 100, Transformation.CENTER);'),
    @('reshape-anchor-out', 'var a = o.pathPoints[2]; a.anchor = [440, 260]; a.leftDirection = [440, 260]; a.rightDirection = [440, 260];'),
    @('reshape-anchor-in', 'var a = o.pathPoints[2]; a.anchor = [300, 200]; a.leftDirection = [300, 200]; a.rightDirection = [300, 200];'),
    @('add-anchor', 'var q = o.pathPoints.add(); q.anchor = [60, 60]; q.leftDirection = [60, 60]; q.rightDirection = [60, 60];'),
    @('remove-anchor', 'o.pathPoints[3].remove();'),
    @('curve-handle', 'var a = o.pathPoints[1]; a.rightDirection = [420, 40]; a.pointType = PointType.CORNER;')
)

foreach ($op in $operations) {
    $name, $code = $op
    Fresh $name
    $srcBefore, $dstBefore = Quads
    Invoke-Fdp ("(function(){{ var o = FDP.named('{0}'); {1} app.redraw(); return 'done'; }})();" -f $name, $code) | Out-Null
    $srcAfter, $dstAfter = Quads
    Check $name ("{0}: the operation leaves the dictionary as it was" -f $name) "src ($srcBefore) dst ($dstBefore)" "src ($srcAfter) dst ($dstAfter)" ($srcAfter -eq $srcBefore -and $dstAfter -eq $dstBefore)
    $before = Drawing $name
    Send-AiMessage 'edit effect' '0' | Out-Null
    $srcAdobe, $dstAdobe = Quads
    $after = Drawing $name
    $changed = ($before[1] -join "`n") -ne ($after[1] -join "`n")
    Check $name ("{0}: Adobe's edit path afterwards writes a rectangular source" -f $name) 'an axis-aligned rectangle' "src ($srcAdobe) dst ($dstAdobe)" (IsRectangle $srcAdobe)
    Check $name ("{0}: and that commit does not change the drawing" -f $name) 'unchanged' $(if ($changed) { 'changed' } else { 'unchanged' }) (-not $changed)

    foreach ($p in $before[0]) { $pointRows.Add(("{0}`tS`t{1}" -f $name, $p)) }
    foreach ($p in $before[1]) { $pointRows.Add(("{0}`tR`t{1}" -f $name, $p)) }
    if ($changed) { foreach ($p in $after[1]) { $pointRows.Add(("{0}`tC`t{1}" -f $name, $p)) } }
    $caseRows.Add(("{0}`t{0}`t{1}`t{2}`t{3}`t{4}`t{5}" -f $name, $srcAfter, $dstAfter, $srcAdobe, $dstAdobe, $(if ($changed) { 'yes' } else { 'no' })))
}

# ---- a blend between two distorted objects ----------------------------------------
# Illustrator interpolates a live effect's parameters along a blend when the
# effect handles it; each expanded step then carries an in-between dictionary.

Invoke-Fdp "FDP.clear(); FDP.pentagon('blendA'); FDP.pentagon('blendB', 420, 60); FDP.selectOnly('blendA');" | Out-Null
Send-AiMessage 'fd append' | Out-Null
Send-AiMessage 'fd write' '0|90,330,340,100|110,350,390,320,70,90,360,120' | Out-Null
Invoke-Fdp "FDP.selectOnly('blendB');" | Out-Null
Send-AiMessage 'fd append' | Out-Null
Send-AiMessage 'fd write' '0|510,390,760,160|470,430,800,390,540,140,700,170' | Out-Null
$made = Invoke-Fdp "(function(){ var d = FDP.doc(); d.selection = null; FDP.named('blendA').selected = true; FDP.named('blendB').selected = true; app.executeMenuCommand('Path Blend Make'); app.redraw(); app.executeMenuCommand('Path Blend Expand'); app.redraw(); return d.selection.length + ' selected after expanding'; })();"
Record 'blend' 'Object > Blend > Make, then Expand' $made
$steps = Invoke-Fdp "(function(){ var d = FDP.doc(); var names = []; function walk(it) { if (it.typename === 'GroupItem') { for (var i = 0; i < it.pageItems.length; i++) walk(it.pageItems[i]); } else { it.name = 'step' + names.length; names.push(it.name); } } for (var k = 0; k < d.selection.length; k++) walk(d.selection[k]); return names.join(','); })();"
$stepNames = @($steps -split ',' | Where-Object { $_ })
$rectangles = 0; $read = 0; $distinct = @{}
foreach ($step in $stepNames) {
    Invoke-Fdp ("FDP.selectOnly('{0}');" -f $step) | Out-Null
    $list = Send-AiMessage 'fd list'
    if ($list -notmatch "object`t0") { continue }
    $s, $d = Quads
    $read++
    if (IsRectangle $s) { $rectangles++ }
    $distinct["$s|$d"] = $true
}
Record 'blend' 'expanded blend steps carrying a Free Distort, and distinct dictionaries among them' ("{0} of {1} objects; {2} distinct" -f $read, $stepNames.Count, $distinct.Count)
Check 'blend' 'every blend step carries a rectangular source' "$read of $read" "$rectangles of $read" ($read -gt 0 -and $rectangles -eq $read)

[IO.File]::WriteAllLines((Join-Path $evidence 'source-follow-cases.tsv'), (Hide-Personal $caseRows))
[IO.File]::WriteAllLines((Join-Path $evidence 'source-follow-points.tsv'), (Hide-Personal $pointRows))
Save-ProbeResults -Path (Join-Path $evidence 'source-follow.tsv')
Save-ProbeTranscript -Path (Join-Path $evidence 'source-follow.txt') -Lines $log
