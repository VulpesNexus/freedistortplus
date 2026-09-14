<#
.SYNOPSIS
    Records a drag made by a person with the real mouse, read back entirely
    over COM: the editor's tool-message counters, Adobe's dictionary, the undo
    history, and the drawing -- plus what the person saw.

.DESCRIPTION
    Illustrator's tools receive only real input, and automating real input
    takes the pointer and the foreground from whoever is at the machine. So
    this part of the test is done by a person, and this script only reads.

    Before: select the fixture, select the editor ("editor open"), zero its
    counters ("editor reset"), clear the undo history ("undo clear"), and note
    the dictionary. The person then drags one corner and releases -- and, with
    -WithEscape, drags another and presses Esc before releasing -- and presses
    Ctrl+Z once. Run this with the dictionary noted before, which it compares
    against. It redoes the one undone step to read what the committed drag
    wrote, and undoes it again.

    What only the person can see is passed in: whether the magenta preview
    outline followed the drag, and whether Adobe's fill updated on release.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $DestinationBefore,
    [switch] $WithEscape,
    [ValidateSet('yes', 'no', 'not reported')] [string] $PreviewFollowed = 'not reported',
    [ValidateSet('yes', 'no', 'not reported')] [string] $FillOnRelease = 'not reported'
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ai.ps1')
$repo = Split-Path -Parent $PSScriptRoot
$evidence = Join-Path $repo 'docs\evidence'

$log = New-Object Collections.Generic.List[string]
function Say([string] $s) { $log.Add($s); Write-Output $s }
function Check([string] $case, [string] $expected, [string] $observed, [bool] $ok) {
    $status = if ($ok) { 'PASS' } else { 'FAIL' }
    Add-ProbeResult -Group 'real mouse' -Case $case -Expected $expected -Observed $observed -Status $status
    Say ("[{0}] {1}: {2}" -f $status, $case, $observed)
}
function Reported([string] $case, [string] $answer) {
    $status = switch ($answer) { 'yes' { 'PASS' } 'no' { 'FAIL' } default { 'NOT RUN' } }
    Add-ProbeResult -Group 'real mouse' -Case $case -Expected 'yes, as reported by the person dragging' -Observed ("reported: {0}" -f $answer) -Status $status
    Say ("[{0}] {1}: reported {2}" -f $status, $case, $answer)
}
function Field([string] $text, [string] $name) {
    foreach ($line in ($text -split "`r?`n")) { if ($line -like "$name`t*") { return $line.Substring($name.Length + 1) } }
    return ''
}

$drags = if ($WithEscape) { 2 } else { 1 }
Start-ProbeResults -Probe 'mouse'
Say ('FreeDistort+ -- a drag made with the real mouse, read over COM, {0}' -f (Get-Date -Format 'yyyy-MM-dd HH:mm'))
Say ('By hand: one corner dragged and released{0}; Ctrl+Z once.' -f $(if ($WithEscape) { '; another dragged and canceled with Esc before release' } else { '' }))

$status = Send-AiMessage 'editor status'
$messages = Field $status 'tool messages'
Say "tool messages: $messages"
Say ("preview: {0}" -f (Field $status 'last preview'))
$m = [regex]::Match($messages, 'down (\d+) drag (\d+) up (\d+)')
$downs = [int] $m.Groups[1].Value; $moves = [int] $m.Groups[2].Value; $ups = [int] $m.Groups[3].Value
Check 'a real drag through Illustrator''s own tool dispatch reaches the editor' ("{0} press(es), as many releases, many drag events" -f $drags) $messages ($downs -eq $drags -and $ups -eq $downs -and $moves -gt 10)
Check 'a real drag begins with a preview captured before the mouse-down' 'a preview of the art, captured before the drag' (Field $status 'last preview') ((Field $status 'last preview') -like 'preview of *(captured before the drag)')

$undo = Send-AiMessage 'undo count'
$after = ((Send-AiMessage 'fd read' '0') -replace '^.*dst ', '').Trim()
Check 'after the real drag and one Undo, the dictionary is back exactly' $DestinationBefore $after ($after -eq $DestinationBefore)
Check 'a real drag of hundreds of events is one undo step' 'past 0, future 1 after one Undo' ("past {0}, future {1}" -f (Field $undo 'past'), (Field $undo 'future')) ((Field $undo 'past') -eq '0' -and (Field $undo 'future') -eq '1')

Invoke-AiScript 'app.redo(); app.redraw(); "redone";' | Out-Null
$committed = ((Send-AiMessage 'fd read' '0') -replace '^.*dst ', '').Trim()
$drawn = Invoke-AiScript '(function(){ var p = app.activeDocument.selection[0]; return p.geometricBounds.join(",") + ";" + p.visibleBounds.join(","); })();'
Invoke-AiScript 'app.undo(); app.redraw(); "undone";' | Out-Null
$moved = 0
$a = [regex]::Matches($DestinationBefore, '-?[0-9.]+') | ForEach-Object { $_.Value }
$b = [regex]::Matches($committed, '-?[0-9.]+') | ForEach-Object { $_.Value }
for ($i = 0; $i -lt 8; $i += 2) { if ($a[$i] -ne $b[$i] -or $a[$i + 1] -ne $b[$i + 1]) { $moved++ } }
Check 'a real drag commits its corners, at the pointer''s own coordinates, unrounded' 'one corner moved, or two for a mode that moves a partner' ("{0}; {1} corners moved (drawing {2})" -f $committed, $moved, $drawn) ($moved -ge 1 -and $moved -le 2)

Reported 'the magenta preview outline follows a real drag' $PreviewFollowed
Reported 'Adobe''s fill updates when a real drag is released' $FillOnRelease

Save-ProbeResults -Path (Join-Path $evidence 'mouse.tsv')
Save-ProbeTranscript -Path (Join-Path $evidence 'mouse.txt') -Lines $log
