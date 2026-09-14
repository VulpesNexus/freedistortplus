<#
.SYNOPSIS
    Records a snap onto another path's anchor made with the real mouse, which
    a scripted drag cannot show: Smart Guides pick up the anchors the pointer
    has passed over, and a scripted drag passes over nothing.

.DESCRIPTION
    -Phase setup builds the fixture and waits for the person: a pentagon
    (bounds 90..340 x 100..330) with a Free Distort, and a triangle whose
    leftmost anchor is at (520.375, 210.625), a point no whole-point drag can
    reach by chance. Smart Guides are switched on if they were off, the view is
    set to 100%, the FreeDistort+ tool is selected, and its counters and the
    undo history are cleared.

    The person then drags the pentagon's top-right handle onto the triangle's
    leftmost corner, releases when Smart Guides label it, and does nothing else.

    -Phase read reads everything back over COM, puts Smart Guides back as they
    were, and writes docs/evidence/manual-snap.txt and manual-snap.tsv.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [ValidateSet('setup', 'read')] [string] $Phase,
    [ValidateSet('yes', 'no', 'not reported')] [string] $AnchorLabelShown = 'not reported'
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ai.ps1')
$repo = Split-Path -Parent $PSScriptRoot
$evidence = Join-Path $repo 'docs\evidence'
$state = Join-Path (Join-Path ([IO.Path]::GetTempPath()) 'fdp-probes') 'manual-snap-state.txt'
$null = New-Item -ItemType Directory -Force -Path (Split-Path -Parent $state)

function Field([string] $text, [string] $name) {
    foreach ($line in ($text -split "`r?`n")) { if ($line -like "$name`t*") { return $line.Substring($name.Length + 1) } }
    return ''
}

if ($Phase -eq 'setup') {
    Initialize-AiSession | Out-Null
    Invoke-Fdp "FDP.clear(); FDP.pentagon('pent'); var t = app.activeDocument.pathItems.add(); t.setEntirePath([[520.375, 210.625], [600, 180], [580, 260]]); t.closed = true; FDP.paint(t); FDP.register('target', t); FDP.selectOnly('pent');" | Out-Null
    Send-AiMessage 'fd append' | Out-Null
    Send-AiMessage 'editor open' | Out-Null
    $wasOn = (Field (Send-AiMessage 'editor status') 'smart guides') -eq 'on'
    if (-not $wasOn) { Invoke-AiScript 'app.executeMenuCommand("Snapomatic on-off menu item"); "toggled";' | Out-Null }
    [IO.File]::WriteAllText($state, $(if ($wasOn) { 'on' } else { 'off' }))
    Invoke-AiScript '(function(){ var v = app.activeDocument.views[0]; v.zoom = 1; v.centerPoint = [360, 215]; app.redraw(); return "zoom"; })();' | Out-Null
    Send-AiMessage 'editor reset' | Out-Null
    Send-AiMessage 'undo clear' | Out-Null
    Write-Output ("Ready. Smart Guides {0}. Drag the pentagon's top-right handle onto the triangle's leftmost corner and release when Smart Guides label it; nothing else." -f $(if ($wasOn) { 'were already on' } else { 'switched on' }))
    return
}

$log = New-Object Collections.Generic.List[string]
function Say([string] $s) { $log.Add($s); Write-Output $s }
function Check([string] $case, [string] $expected, [string] $observed, [bool] $ok) {
    $status = if ($ok) { 'PASS' } else { 'FAIL' }
    Add-ProbeResult -Group 'real mouse' -Case $case -Expected $expected -Observed $observed -Status $status
    Say ("[{0}] {1}: {2}" -f $status, $case, $observed)
}

Start-ProbeResults -Probe 'manual-snap'
Say ('FreeDistort+ -- a snap onto another path''s anchor, made with the real mouse, read over COM, {0}' -f (Get-Date -Format 'yyyy-MM-dd HH:mm'))
Say 'By hand: the pentagon''s top-right handle dragged onto the triangle''s leftmost corner at (520.375, 210.625), released when Smart Guides labeled it.'

$status = Send-AiMessage 'editor status'
$messages = Field $status 'tool messages'
Say ("smart guides: {0}; last snap: {1}" -f (Field $status 'smart guides'), (Field $status 'last snap'))
Check 'the drag was one real drag through Illustrator''s own tool dispatch' 'one press, one release, many drag events' $messages ($messages -match '^down 1 drag (\d+) up 1' -and [int] ([regex]::Match($messages, 'drag (\d+)').Groups[1].Value) -gt 10)

$dst = ((Send-AiMessage 'fd read' '0') -replace '^.*dst ', '').Trim()
$want = '(90,330 520.375,210.625 90,100 340,100)'
Check 'a real drag onto another path''s anchor, which the pointer passed over, lands on the anchor exactly' $want $dst ($dst -eq $want)
$undo = Send-AiMessage 'undo count'
Check '...as one undo step' 'past 1' ("past {0}" -f (Field $undo 'past')) ((Field $undo 'past') -eq '1')

$status = switch ($AnchorLabelShown) { 'yes' { 'PASS' } 'no' { 'FAIL' } default { 'NOT RUN' } }
Add-ProbeResult -Group 'real mouse' -Case 'Smart Guides labeled the anchor during the drag, as for Illustrator''s own tools' -Expected 'yes, as reported by the person' -Observed ("reported: {0}" -f $AnchorLabelShown) -Status $status
Say ("[{0}] Smart Guides labeled the anchor during the drag: reported {1}" -f $status, $AnchorLabelShown)

if ((Test-Path $state) -and [IO.File]::ReadAllText($state) -eq 'off') {
    Invoke-AiScript 'app.executeMenuCommand("Snapomatic on-off menu item"); "toggled";' | Out-Null
    Say 'Smart Guides switched back off.'
}
if (Test-Path $state) { [IO.File]::Delete($state) }

Save-ProbeResults -Path (Join-Path $evidence 'manual-snap.tsv')
Save-ProbeTranscript -Path (Join-Path $evidence 'manual-snap.txt') -Lines $log
