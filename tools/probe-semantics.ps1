<#
.SYNOPSIS
    What each key of Adobe Free Distort's dictionary means, measured by
    changing it and looking at the drawing; and how the stored quads relate to
    the art when the art moves, scales, rotates, or changes shape.

.DESCRIPTION
    Needs Illustrator running with EnhancedFreeDistort.aip loaded. Writes
    docs/evidence/dictionary.txt, semantics.txt and semantics.tsv.
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
function Field([string] $text, [string] $name) {
    foreach ($line in ($text -split "`r?`n")) { if ($line -like "$name`t*") { return $line.Substring($name.Length + 1) } }
    return ''
}
function Result([string] $name) { ((Invoke-Efd ("EFD.sourceAndResult('{0}');" -f $name)) -split "`r?`n" | Where-Object { $_ -like 'R*' }) -join '|' }
function Silent { Invoke-AiScript 'app.userInteractionLevel = UserInteractionLevel.DONTDISPLAYALERTS; "off";' | Out-Null; Send-AiMessage 'edit effect' '0' | Out-Null }
function Read0 { (Send-AiMessage 'fd read' '0').Trim() }

Start-ProbeResults -Probe 'semantics'
Say ('Enhanced Free Distort -- dictionary semantics, {0}' -f (Get-Date -Format 'yyyy-MM-dd HH:mm'))
Initialize-AiSession | Out-Null

# ---- the complete dictionary, as Adobe writes it ---------------------------------

Invoke-Efd 'EFD.clear(); EFD.pentagon("pent"); EFD.selectOnly("pent");' | Out-Null
Send-AiMessage 'fd append' | Out-Null
$empty = Send-AiMessage appearance
Record 'dictionary' 'a Free Distort added without its dialog holds' ([regex]::Match($empty, '# \d+ entries').Value)
$identity = Result 'pent'
$sourceRows = ((Invoke-Efd 'EFD.sourceAndResult("pent");') -split "`r?`n" | Where-Object { $_ -like 'S*' }) -join '|'
Check 'dictionary' 'an empty dictionary draws the art unchanged' 'result = source' $(if ($identity -eq ($sourceRows -replace '(^|\|)S', '$1R')) { 'identical' } else { 'different' }) ($identity -eq ($sourceRows -replace '(^|\|)S', '$1R'))

Silent
$committed = Send-AiMessage appearance
[System.IO.File]::WriteAllLines((Join-Path $evidence 'dictionary.txt'), (Hide-Personal @(
    'Adobe Free Distort, as its own edit path commits it on an untouched pentagon',
    'with geometric bounds 90..340 by 100..330 (Illustrator artwork coordinates, y up).',
    '', $committed.TrimEnd())))
$keys = [regex]::Matches($committed, '(?m)^\s+(\S+) \((\w+)\) = (.*)$') | ForEach-Object { [pscustomobject]@{ Key = $_.Groups[1].Value; Type = $_.Groups[2].Value; Value = $_.Groups[3].Value.Trim() } }
Record 'dictionary' "Adobe's commit writes these keys" (($keys | ForEach-Object { "{0} {1}" -f $_.Key, $_.Type }) -join ', ')
Check 'dictionary' 'the source quad is the geometric bounds, corner order TL TR BL BR' 'src0 90,330 src1 340,330 src2 90,100 src3 340,100' (Read0) ((Read0) -match 'src \(90,330 340,330 90,100 340,100\) dst \(90,330 340,330 90,100 340,100\)')

# ---- which keys the renderer reads ----------------------------------------------------

# Start from a distorted state so that a change to any key that matters shows.
Send-AiMessage 'fd write' '0|90,330,340,100|110,350,390,320,70,90,360,120' | Out-Null
$base = Result 'pent'
foreach ($k in $keys) {
    $original = Send-AiMessage appearance
    $current = [regex]::Match($original, ('(?m)^\s+{0} \((\w+)\) = (.*)$' -f [regex]::Escape($k.Key)))
    if (-not $current.Success) { continue }
    $type = $current.Groups[1].Value; $value = $current.Groups[2].Value.Trim()
    switch ($type) {
        'Real'    { $new = Format-AiNumber ([double]::Parse($value, $inv) + 37); $t = 'real' }
        'Boolean' { $new = if ($value -eq 'true') { 'false' } else { 'true' }; $t = 'bool' }
        'Integer' { $new = [string] ([int] $value + 1); $t = 'int' }
        default   { Record 'keys' ("{0} ({1})" -f $k.Key, $type) 'not a type this probe perturbs'; continue }
    }
    Send-AiMessage 'set param' ("0|{0}|{1}|{2}" -f $k.Key, $t, $new) | Out-Null
    $changed = (Result 'pent') -ne $base
    Send-AiMessage 'set param' ("0|{0}|{1}|{2}" -f $k.Key, $t, $value) | Out-Null
    $restored = (Result 'pent') -eq $base
    Record 'keys' ("{0} ({1}) {2} -> {3}" -f $k.Key, $type, $value, $new) ("drawing {0}; restoring the value {1}" -f $(if ($changed) { 'changed' } else { 'unchanged' }), $(if ($restored) { 'restores it exactly' } else { 'does NOT restore it' }))
}

Send-AiMessage 'delete param' '0|-DefaultApplyEffectsKey' | Out-Null
Check 'keys' '-DefaultApplyEffectsKey can be absent without changing the drawing' 'unchanged' $(if ((Result 'pent') -eq $base) { 'unchanged' } else { 'changed' }) ((Result 'pent') -eq $base)

Send-AiMessage 'set param' '0|efdProbeKey|real|123' | Out-Null
Check 'keys' 'a key Adobe does not know changes nothing in the drawing' 'unchanged' $(if ((Result 'pent') -eq $base) { 'unchanged' } else { 'changed' }) ((Result 'pent') -eq $base)
Silent
$afterVanilla = Send-AiMessage appearance
Record 'keys' "a key Adobe does not know, after Adobe's own edit path commits" $(if ($afterVanilla -match 'efdProbeKey') { 'kept' } else { 'dropped' })
Send-AiMessage 'delete param' '0|efdProbeKey' | Out-Null

# ---- source and destination under changes to the art ------------------------------------

function Fresh {
    Invoke-Efd 'EFD.clear(); EFD.pentagon("pent"); EFD.selectOnly("pent");' | Out-Null
    Send-AiMessage 'fd append' | Out-Null
    Send-AiMessage 'fd write' '0|90,330,340,100|90,330,400,330,90,100,340,100' | Out-Null
}

Fresh
$stored = Read0
Invoke-Efd 'EFD.named("pent").translate(50, 20); app.redraw(); "moved";' | Out-Null
Check 'art changes' 'moving the art does not touch the dictionary' $stored (Read0) ((Read0) -eq $stored)
$bounds = Invoke-Efd 'EFD.bounds("pent");'
$right = [double]::Parse(((($bounds -split ';')[1]) -split ',')[2], $inv)
Check 'art changes' 'the drawing moves with the art (renormalized onto the moved bounds)' (Format-AiNumber (390 + 60 * 160 / 230)) (Format-AiNumber $right) ([math]::Abs($right - (390 + 60 * 160 / 230)) -lt 1e-6)
Silent
Check 'art changes' "Adobe's edit path then rewrites the source as the moved bounds and renormalizes the destination" 'src (140,350 390,350 140,120 390,120) dst (140,350 450,350 140,120 390,120)' (Read0) ((Read0) -match 'src \(140,350 390,350 140,120 390,120\) dst \(140,350 450,350 140,120 390,120\)')

Fresh
Invoke-Efd 'EFD.named("pent").resize(150, 150, true, true, true, true, 150, Transformation.CENTER); app.redraw(); "scaled";' | Out-Null
Check 'art changes' 'scaling the art (scale strokes and effects on) does not touch the dictionary' '90..340 source' (Read0) ((Read0) -match 'src \(90,330 340,330 90,100 340,100\) dst \(90,330 400,330 90,100 340,100\)')
Silent
Check 'art changes' 'after scaling 150% about the center, the normalized distortion is kept: dst1h = 27.5 + 1.24 x 375' 'src 27.5..402.5 by 42.5..387.5, dst1h 492.5' (Read0) ((Read0) -match 'src \(27.5,387.5 402.5,387.5 27.5,42.5 402.5,42.5\) dst \(27.5,387.5 492.5,387.5 27.5,42.5 402.5,42.5\)')

Fresh
Invoke-Efd 'EFD.named("pent").rotate(30); app.redraw(); "rotated";' | Out-Null
$rotated = Invoke-Efd 'EFD.bounds("pent");'
Silent
Record 'art changes' 'after rotating the art 30 degrees, Adobe re-lays the quad onto the new axis-aligned bounds' ("bounds {0}; {1}" -f $rotated, (Read0))

Fresh
Invoke-Efd 'var p = EFD.named("pent"); var a = p.pathPoints[2]; a.anchor = [440, 260]; a.leftDirection = [440, 260]; a.rightDirection = [440, 260]; app.redraw(); "reshaped";' | Out-Null
Check 'art changes' 'editing an anchor so the bounds grow does not touch the dictionary' '90..340 source' (Read0) ((Read0) -match 'src \(90,330 340,330 90,100 340,100\)')
Silent
Check 'art changes' "after the reshape, Adobe's edit path renormalizes onto 90..440" 'dst1h = 440 + 60/250 x 350 = 524' (Read0) ((Read0) -match 'src \(90,330 440,330 90,100 440,100\) dst \(90,330 524,330 90,100 440,100\)')

# ---- which bounds ---------------------------------------------------------------------------

Invoke-Efd 'EFD.clear(); EFD.stroked("s"); EFD.selectOnly("s");' | Out-Null
Send-AiMessage 'fd append' | Out-Null
$b = Send-AiMessage 'fd bounds' '0'
Check 'input bounds' 'a 20 pt stroke: the input bounds are the geometric bounds, not the visible ones' '[90 330 340 100]' (Field $b 'input bounds') ((Field $b 'input bounds') -eq '[90 330 340 100]')

Invoke-Efd 'EFD.clear(); EFD.pentagon("t"); EFD.selectOnly("t");' | Out-Null
Send-AiMessage 'apply effect' 'Adobe Transform|scaleH_Factor=r:0.5;scaleV_Factor=r:0.5;moveH_Pts=r:30;moveV_Pts=r:0' | Out-Null
Send-AiMessage 'fd append' | Out-Null
$b = Send-AiMessage 'fd bounds' '1'
Check 'input bounds' 'under a Transform effect (50%, moved 30 pt), the input bounds are the transformed art' 'Adobe, [182.5 272.5 307.5 157.5]' ("{0}, {1}" -f (Field $b 'from'), (Field $b 'input bounds')) ((Field $b 'from') -eq 'Adobe' -and (Field $b 'input bounds') -eq '[182.5 272.5 307.5 157.5]')
Record 'input bounds' 'geometric bounds of the same object, for contrast' (Field $b 'geometric bounds')

Save-ProbeResults -Path (Join-Path $evidence 'semantics.tsv')
Save-ProbeTranscript -Path (Join-Path $evidence 'semantics.txt') -Lines $log
