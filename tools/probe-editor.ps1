<#
.SYNOPSIS
    The editing loop: drags run through the editor's own drag code, and what
    they leave in Adobe's dictionary, in the drawing, and in the undo history.

.DESCRIPTION
    The bridge's "editor drag" selector runs BeginDrag, one StepDrag per point,
    and EndDrag -- the code the mouse handlers run -- without a mouse, so every
    case is repeatable. What the real mouse adds on top (hit-testing a handle,
    the host's own drag dispatch) is covered by probe-mouse.ps1.

    Needs Illustrator running with EnhancedFreeDistort.aip loaded. Writes
    docs/evidence/editor.txt and editor.tsv.
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
function Field([string] $text, [string] $name) {
    foreach ($line in ($text -split "`r?`n")) { if ($line -like "$name`t*") { return $line.Substring($name.Length + 1) } }
    return ''
}
function Past { [int] (Field (Send-AiMessage 'undo count') 'past') }
function Dst { ((Send-AiMessage 'fd read' '0') -replace '^.*dst ', '').Trim() }
function Fresh([string] $name = 'pent') {
    Invoke-Efd ("EFD.clear(); EFD.pentagon('{0}'); EFD.selectOnly('{0}');" -f $name) | Out-Null
    Send-AiMessage 'fd append' | Out-Null
    Send-AiMessage 'editor refresh' 'measure' | Out-Null
}

Start-ProbeResults -Probe 'editor'
Say ('Enhanced Free Distort -- editing loop, {0}' -f (Get-Date -Format 'yyyy-MM-dd HH:mm'))
Initialize-AiSession | Out-Null

# ---- opening --------------------------------------------------------------------

Invoke-Efd 'EFD.clear(); EFD.pentagon("pent"); EFD.selectOnly("pent");' | Out-Null
$opened = Send-AiMessage 'editor open'
$status = Send-AiMessage 'editor status'
Say ($opened.TrimEnd()); Say ($status.TrimEnd())
Check 'open' 'the command adds a Free Distort when there is none and selects the editor' 'active, target valid, bounds from Adobe' ("{0}, {1}, {2}" -f (Field $status 'active'), (Field $status 'target'), (Field $status 'bounds from')) ((Field $status 'active') -eq 'yes' -and (Field $status 'target') -eq 'valid' -and (Field $status 'bounds from') -eq 'Adobe')
Check 'open' 'an untouched effect shows its handles on the input bounds' '(90,330 340,330 90,100 340,100)' (Field $status 'quad') ((Field $status 'quad') -eq '(90,330 340,330 90,100 340,100)')

# ---- one free drag, one undo step ---------------------------------------------------

Fresh
# Illustrator's history holds 100 steps; a count taken at the cap cannot rise.
Send-AiMessage 'undo clear' | Out-Null
$pastBefore = Past
$drag = Send-AiMessage 'editor drag' '2|free|-1|85,95;75,85;65,75;60,70'
Say ($drag.TrimEnd())
$pastAfter = Past
Check 'drag' 'a free drag leaves the corner where it was released' '(90,330 340,330 60,70 340,100)' (Dst) ((Dst) -eq '(90,330 340,330 60,70 340,100)')
Check 'drag' 'four drag steps make one undo step' ("past {0}" -f ($pastBefore + 1)) ("past {0}" -f $pastAfter) ($pastAfter -eq $pastBefore + 1)
Invoke-AiScript 'app.undo(); app.redraw(); "undone";' | Out-Null
Check 'drag' 'one Undo takes the whole drag back' '(90,330 340,330 90,100 340,100) or empty' (Dst) ((Dst) -eq '(90,330 340,330 90,100 340,100)' -or (Dst) -match 'src \(absent\)|^\(absent\)|entries 0')
Invoke-AiScript 'app.redo(); app.redraw(); "redone";' | Out-Null
Check 'drag' 'Redo puts it back' '(90,330 340,330 60,70 340,100)' (Dst) ((Dst) -eq '(90,330 340,330 60,70 340,100)')

# ---- no drift -------------------------------------------------------------------------

Fresh
Send-AiMessage 'editor drag' '1|free|-1|410,360' | Out-Null
$reference = Dst
$renderReference = Invoke-Efd 'app.redraw(); EFD.bounds("pent");'
$wild = (1..60 | ForEach-Object { '{0},{1}' -f (Format-AiNumber (410 + 173.1 * [math]::Sin($_ * 0.7))), (Format-AiNumber (360 + 91.7 * [math]::Cos($_ * 1.3))) }) -join ';'
Send-AiMessage 'editor drag' ("1|free|-1|{0};410,360" -f $wild) | Out-Null
Check 'drift' 'sixty wild steps that end where they began change nothing' $reference (Dst) ((Dst) -eq $reference)
$render = Invoke-Efd 'app.redraw(); EFD.bounds("pent");'
Check 'drift' '...and the drawing is the same to the nanopoint' $renderReference $render ($render -eq $renderReference)

# ---- Escape ---------------------------------------------------------------------------

Fresh
Send-AiMessage 'editor drag' '0|free|-1|70,350' | Out-Null
$reference = Dst
Send-AiMessage 'undo clear' | Out-Null
$pastBefore = Past
$cancel = Send-AiMessage 'editor drag' '3|free|2|360,90;380,80;400,60;420,40'
Say ($cancel.TrimEnd())
Check 'cancel' 'Escape part-way through a drag restores the dictionary exactly' $reference (Dst) ((Dst) -eq $reference)
Check 'cancel' 'a canceled drag leaves no undo step' ("past {0}" -f $pastBefore) ("past {0}" -f (Past)) ((Past) -eq $pastBefore)

# ---- modes -----------------------------------------------------------------------------

Fresh
Send-AiMessage 'editor drag' '0|perspective|-1|120,332' | Out-Null
Check 'modes' 'perspective: a mostly horizontal drag narrows the top edge about its midpoint' '(120,330 310,330 90,100 340,100)' (Dst) ((Dst) -eq '(120,330 310,330 90,100 340,100)')
Fresh
Send-AiMessage 'editor drag' '3|perspective|-1|343,130' | Out-Null
Check 'modes' 'perspective: a mostly vertical drag shortens the right edge about its midpoint' '(90,330 340,300 90,100 340,130)' (Dst) ((Dst) -eq '(90,330 340,300 90,100 340,130)')
Fresh
Send-AiMessage 'editor drag' '0|symmetric|-1|100,320' | Out-Null
Check 'modes' 'symmetric: the opposite corner moves the other way' '(100,320 340,330 90,100 330,110)' (Dst) ((Dst) -eq '(100,320 340,330 90,100 330,110)')
Fresh
Send-AiMessage 'editor drag' '1|affine|-1|380,330' | Out-Null
$affine = Dst
$n = [regex]::Matches($affine, '-?[0-9.]+') | ForEach-Object { [double]::Parse($_.Value, [Globalization.CultureInfo]::InvariantCulture) }
$gap = [math]::Abs(($n[0] + $n[6]) - ($n[2] + $n[4])) + [math]::Abs(($n[1] + $n[7]) - ($n[3] + $n[5]))
Check 'modes' 'affine: the result is a parallelogram with the dragged corner under the pointer' 'TL+BR = TR+BL, corner 1 at 380,330' ("{0}, mismatch {1}" -f $affine, (Format-AiNumber $gap)) ($gap -lt 1e-6 -and $n[2] -eq 380 -and $n[3] -eq 330)

# ---- a duplicate that shares the style ----------------------------------------------------

Fresh 'original'
Send-AiMessage 'editor drag' '1|free|-1|400,330' | Out-Null
Invoke-Efd 'var o = EFD.named("original"); var c = o.duplicate(); c.name = "copy"; EFD.selectOnly("copy"); "dup";' | Out-Null
Send-AiMessage 'editor refresh' 'measure' | Out-Null
Send-AiMessage 'editor drag' '2|free|-1|40,60' | Out-Null
$copy = Dst
Invoke-Efd 'EFD.selectOnly("original");' | Out-Null
$original = Dst
Check 'independence' 'editing a duplicate leaves the original that shared its style alone' '(90,330 400,330 90,100 340,100)' $original ($original -eq '(90,330 400,330 90,100 340,100)' -and $copy -eq '(90,330 400,330 40,60 340,100)')

# ---- the art moves under the editor --------------------------------------------------------

Fresh
Send-AiMessage 'editor drag' '1|free|-1|400,330' | Out-Null
Invoke-Efd 'EFD.named("pent").translate(100, 50); app.redraw(); "moved";' | Out-Null
$estimated = Field (Send-AiMessage 'editor refresh') 'quad'
$measured = Field (Send-AiMessage 'editor refresh' 'measure') 'quad'
Check 'moved art' 'after the art moves, the handles move with it' '(190,380 500,380 190,150 440,150)' $measured ($measured -eq '(190,380 500,380 190,150 440,150)')
Check 'moved art' 'the estimate made without asking Adobe agrees with asking' $measured $estimated ($estimated -eq $measured)
$pastBefore = Past
Send-AiMessage 'editor drag' '0|free|-1|180,390' | Out-Null
Check 'moved art' 'a drag after the move writes source = the new input bounds' 'src (190,380 440,380 190,150 440,150)' ((Send-AiMessage 'fd read' '0').Trim()) ((Send-AiMessage 'fd read' '0') -match 'src \(190,380 440,380 190,150 440,150\) dst \(180,390 500,380 190,150 440,150\)')

# ---- several Free Distorts on one object ------------------------------------------------------

Fresh
Send-AiMessage 'fd append' | Out-Null
Send-AiMessage 'editor refresh' 'measure' | Out-Null
$listed = (Send-AiMessage 'fd list').Trim()
$status = Send-AiMessage 'editor status'
Check 'instances' 'two Free Distorts on one object are both found, and the editor says which it took' 'post-effects 0 and 1; one chosen, reason given' ("{0}; post-effect {1}, instances {2}, by focus {3}" -f ($listed -replace "`t", ' '), (Field $status 'post-effect'), (Field $status 'instances'), (Field $status 'chosen by focus')) ($listed -eq "object`t0`t1" -and (Field $status 'instances') -eq '2')
$chosen = [int] (Field $status 'post-effect')
$other = 1 - $chosen
$otherBefore = (Send-AiMessage 'fd read' ([string] $other)).Trim()
$quad = (Field $status 'quad')
$n = [regex]::Matches($quad, '-?[0-9.]+') | ForEach-Object { [double]::Parse($_.Value, [Globalization.CultureInfo]::InvariantCulture) }
Send-AiMessage 'editor drag' ("0|free|-1|{0},{1}" -f (Format-AiNumber ($n[0] - 20)), (Format-AiNumber ($n[1] + 15))) | Out-Null
$chosenAfter = (Send-AiMessage 'fd read' ([string] $chosen)).Trim()
$otherAfter = (Send-AiMessage 'fd read' ([string] $other)).Trim()
Check 'instances' 'a drag edits only the Free Distort the editor chose' $otherBefore $otherAfter ($otherAfter -eq $otherBefore -and $chosenAfter -ne $otherBefore)

# ---- a source that is not a rectangle ---------------------------------------------------------

Fresh
Send-AiMessage 'set param' '0|src0h|real|90' | Out-Null
foreach ($k in 'src0v=330','src1h=340','src1v=330','src2h=110','src2v=100','src3h=340','src3v=100','dst0h=90','dst0v=330','dst1h=400','dst1v=330','dst2h=90','dst2v=100','dst3h=340','dst3v=100') {
    $kv = $k -split '='
    Send-AiMessage 'set param' ("0|{0}|real|{1}" -f $kv[0], $kv[1]) | Out-Null
}
$status = Send-AiMessage 'editor refresh' 'measure'
Check 'refusals' 'an effect whose source is not a rectangle is not edited, and the status says why' 'no target, reason given' (Field $status 'why') ((Field $status 'target') -eq 'none' -and (Field $status 'why') -match 'not a rectangle')
Save-ProbeResults -Path (Join-Path $evidence 'editor.tsv')
Save-ProbeTranscript -Path (Join-Path $evidence 'editor.txt') -Lines $log
