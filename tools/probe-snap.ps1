<#
.SYNOPSIS
    Snapping: drags run through Illustrator's own Smart Guides engine, with
    this tool's own targets added, measured in the host.

.DESCRIPTION
    The bridge's "editor drag" with a "+snap" mode passes every point through
    AICursorSnapSuite::Track, as a mouse drag does. A pentagon (bounds
    90..340 x 100..330) carries the Free Distort; a second path has an anchor
    at (520.375, 210.625), away from it. Smart Guides are switched on for the
    run if they were off, and put back afterwards.

    Needs Illustrator running with FreeDistortPlus.aip loaded. Writes
    docs/evidence/snap.txt and snap.tsv.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ai.ps1')
$repo = Split-Path -Parent $PSScriptRoot
$evidence = Join-Path $repo 'docs\evidence'

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
function Dst { ((Send-AiMessage 'fd read' '0') -replace '^.*dst ', '').Trim() }
function SmartGuides { Field (Send-AiMessage 'editor status') 'smart guides' }
function Zoom([double] $zoom) { Invoke-AiScript ('(function(){{ var v = app.activeDocument.views[0]; v.zoom = {0}; v.centerPoint = [300, 215]; app.redraw(); return "zoom"; }})();' -f (Format-AiNumber $zoom)) | Out-Null }
function Fresh {
    Invoke-Fdp "FDP.clear(); FDP.pentagon('pent'); var t = app.activeDocument.pathItems.add(); t.setEntirePath([[520.375, 210.625], [600, 180], [580, 260]]); t.closed = true; FDP.paint(t); FDP.register('target', t); FDP.selectOnly('pent');" | Out-Null
    Send-AiMessage 'fd append' | Out-Null
    Send-AiMessage 'editor open' | Out-Null
}
function Drag([string] $mode, [string] $to) {
    $said = Send-AiMessage 'editor drag' ("1|{0}|-1|{1}" -f $mode, $to)
    return @((Dst), (Field (Send-AiMessage 'editor status') 'last snap'))
}

Start-ProbeResults -Probe 'snap'
Say ('FreeDistort+ -- snapping, {0}' -f (Get-Date -Format 'yyyy-MM-dd HH:mm'))
Initialize-AiSession | Out-Null
Fresh
$wasOn = (SmartGuides) -eq 'on'
if (-not $wasOn) { Invoke-AiScript 'app.executeMenuCommand("Snapomatic on-off menu item"); "toggled";' | Out-Null }
Record 'setup' 'Smart Guides before the run' $(if ($wasOn) { 'on' } else { 'off, switched on for the run' })
Zoom 1

Fresh; Zoom 1
$d, $snap = Drag 'free+snap' '341.2,250.3'
Check 'targets' "Illustrator's engine snaps the corner onto the art's own bounding-box guide, as for its own tools" '(90,330 340,250.3 90,100 340,100)' ("{0}; {1}" -f $d, $snap) ($d -eq '(90,330 340,250.3 90,100 340,100)')
Fresh; Zoom 1
$d, $snap = Drag 'free+snap' '521.5,211.5'
Record 'targets' "another path's anchor 1.6 pt away, with no pointer having passed over that path: the engine's answer" ("{0}; {1}" -f $d, $snap)

Fresh; Zoom 1
$d, $snap = Drag 'free+snap' '341.2,331.1'
Check 'targets' 'a corner released near where it is undistorted lands there exactly' '(90,330 340,330 90,100 340,100)' ("{0}; {1}" -f $d, $snap) ($d -eq '(90,330 340,330 90,100 340,100)')

Fresh; Zoom 1
$d, $snap = Drag 'free+snap' '215.9,214.2'
Check 'targets' "a corner released near the art's undistorted center lands on it" '(90,330 215,215 90,100 340,100)' ("{0}; {1}" -f $d, $snap) ($d -eq '(90,330 215,215 90,100 340,100)')

Fresh; Zoom 1
$d, $snap = Drag 'free+snap' '640.5,440.3'
Check 'targets' 'far from every target, the corner goes where the pointer is' '(90,330 640.5,440.3 90,100 340,100)' ("{0}; {1}" -f $d, $snap) ($d -eq '(90,330 640.5,440.3 90,100 340,100)')

Fresh; Zoom 1
$d, $snap = Drag 'axis+snap' '216.2,331.8'
Check 'constraint' 'with Shift, a snap never breaks the axis lock: the snapped x is kept, y stays where it was' '(90,330 215,330 90,100 340,100)' ("{0}; {1}" -f $d, $snap) ($d -eq '(90,330 215,330 90,100 340,100)')

Fresh; Zoom 6
$d, $snap = Drag 'free+snap' '341.6,331.2'
Check 'zoom' 'the reach is in screen pixels: at 600% a corner 2 pt from where it is undistorted does not snap back' '(90,330 341.6,331.2 90,100 340,100)' ("{0}; {1}" -f $d, $snap) ($d -eq '(90,330 341.6,331.2 90,100 340,100)')
Fresh; Zoom 6
$d, $snap = Drag 'free+snap' '340.4,330.3'
Check 'zoom' '...while at 600% half a point still does' '(90,330 340,330 90,100 340,100)' ("{0}; {1}" -f $d, $snap) ($d -eq '(90,330 340,330 90,100 340,100)')

Fresh; Zoom 1
Invoke-AiScript 'app.executeMenuCommand("Snapomatic on-off menu item"); "toggled";' | Out-Null
$d, $snap = Drag 'free+snap' '341.2,331.1'
Check 'preference' "with View > Smart Guides off, this tool's own targets do not snap" '(90,330 341.2,331.1 90,100 340,100)' ("{0}; {1}; smart guides {2}" -f $d, $snap, (SmartGuides)) ($d -eq '(90,330 341.2,331.1 90,100 340,100)')
Fresh; Zoom 1
$d, $snap = Drag 'free+snap' '521.5,211.5'
Record 'preference' "with View > Smart Guides off, another path's anchor 1.6 pt away, under this machine's View > Snap to Point setting: the engine's answer" ("{0}; {1}" -f $d, $snap)
Invoke-AiScript 'app.executeMenuCommand("Snapomatic on-off menu item"); "toggled";' | Out-Null

if (-not $wasOn) { Invoke-AiScript 'app.executeMenuCommand("Snapomatic on-off menu item"); "restored";' | Out-Null }
Record 'setup' 'Smart Guides after the run' (SmartGuides)
Save-ProbeResults -Path (Join-Path $evidence 'snap.tsv')
Save-ProbeTranscript -Path (Join-Path $evidence 'snap.txt') -Lines $log
