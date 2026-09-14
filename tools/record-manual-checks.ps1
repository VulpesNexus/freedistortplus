<#
.SYNOPSIS
    Records the checks only a person at Illustrator can make -- how the tool's
    icon looks, whether a double-click and a menu open their windows -- and
    reads back over COM what a real click and real arrow keys did.

.DESCRIPTION
    Before: a pentagon with a Free Distort whose top-right corner is at
    (400, 360), the FreeDistort+ tool active, its counters zeroed ("editor
    reset"), and the undo history cleared ("undo clear"). The person then
    looks at the icon, reads Edit > Preferences > General > Keyboard
    Increment, double-clicks the tool's icon and cancels the dialog, opens
    Help > About VulpesNexus Plug-ins > FreeDistort+, clicks the top-right
    handle once, and presses Right three times and Shift+Right once.

    What only the person can see is passed in. Writes
    docs/evidence/manual-checks.txt and manual-checks.tsv.
#>
[CmdletBinding()]
param(
    [ValidateSet('yes', 'no', 'not reported')] [string] $IconMatchesTheme = 'not reported',
    [string] $KeyboardIncrementShown = '',
    [ValidateSet('yes', 'no', 'not reported')] [string] $DoubleClickOpensDialog = 'not reported',
    [ValidateSet('yes', 'no', 'not reported')] [string] $AboutWindowRight = 'not reported'
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ai.ps1')
$repo = Split-Path -Parent $PSScriptRoot
$evidence = Join-Path $repo 'docs\evidence'
$inv = [Globalization.CultureInfo]::InvariantCulture

$log = New-Object Collections.Generic.List[string]
function Say([string] $s) { $log.Add($s); Write-Output $s }
function Check([string] $case, [string] $expected, [string] $observed, [bool] $ok) {
    $status = if ($ok) { 'PASS' } else { 'FAIL' }
    Add-ProbeResult -Group 'by hand' -Case $case -Expected $expected -Observed $observed -Status $status
    Say ("[{0}] {1}: {2}" -f $status, $case, $observed)
}
function Reported([string] $case, [string] $answer) {
    $status = switch ($answer) { 'yes' { 'PASS' } 'no' { 'FAIL' } default { 'NOT RUN' } }
    Add-ProbeResult -Group 'by hand' -Case $case -Expected 'yes, as reported by the person' -Observed ("reported: {0}" -f $answer) -Status $status
    Say ("[{0}] {1}: reported {2}" -f $status, $case, $answer)
}
function Field([string] $text, [string] $name) {
    foreach ($line in ($text -split "`r?`n")) { if ($line -like "$name`t*") { return $line.Substring($name.Length + 1) } }
    return ''
}

Start-ProbeResults -Probe 'manual'
Say ('FreeDistort+ -- checks made by hand at Illustrator, read over COM, {0}' -f (Get-Date -Format 'yyyy-MM-dd HH:mm'))

Reported 'the tool icon matches the other tools on the dark interface' $IconMatchesTheme
Reported 'a double-click on the tool icon opens the corners dialog' $DoubleClickOpensDialog
Reported 'Help > About VulpesNexus Plug-ins > FreeDistort+ opens the templated About window' $AboutWindowRight

$status = Send-AiMessage 'editor status'
$increment = Field $status 'keyboard increment'
$shownNumber = ($KeyboardIncrementShown -replace '[^0-9,.\-]', '') -replace ',', '.'
Check "the plugin's keyboard increment is the one Illustrator's preferences show" $KeyboardIncrementShown ("plugin reads {0} pt" -f $increment) ($shownNumber -ne '' -and [double]::Parse($shownNumber, $inv) -eq [double]::Parse($increment, $inv))

$messages = Field $status 'tool messages'
Check 'a real click on a handle selects that corner without moving it' 'one press and release on corner 1; active corner 1' ("{0}; active corner {1}" -f $messages, (Field $status 'active corner')) ((Field $status 'active corner') -eq '1' -and $messages -match 'down 1 .* up 1 last hit 1')

$dst = ((Send-AiMessage 'fd read' '0') -replace '^.*dst ', '').Trim()
$step = [double]::Parse($increment, $inv)
$want = '(90,330 {0},360 90,100 340,100)' -f (Format-AiNumber ([math]::Round(400 + 13 * $step, 9)))
Check 'Right three times and Shift+Right once move the selected corner by 3 + 10 increments' $want $dst ($dst -eq $want)
$undo = Send-AiMessage 'undo count'
Check '...one undo step per key press' 'past 4' ("past {0}" -f (Field $undo 'past')) ((Field $undo 'past') -eq '4')
$geometry = Invoke-AiScript '(function(){ return app.activeDocument.pageItems.getByName("pent").geometricBounds.join(","); })();'
Check '...and Illustrator does not also nudge the art' '90,330,340,100' $geometry ($geometry -eq '90,330,340,100')

Save-ProbeResults -Path (Join-Path $evidence 'manual-checks.tsv')
Save-ProbeTranscript -Path (Join-Path $evidence 'manual-checks.txt') -Lines $log
