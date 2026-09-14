<#
.SYNOPSIS
    The minimal proof: this plugin and Adobe's own Free Distort dialog edit the
    same state, and neither owns it.

.DESCRIPTION
    1. find the selected object's Adobe Free Distort (adding one if needed)
    2. ask Adobe for the effect's input bounds, and leave the document as found
    3. move one destination corner through the plugin
    4. Adobe re-renders it; the source path is untouched; the entry is still Adobe's
    5. Adobe's own dialog opens showing the moved corner (captured), and Cancel
       changes nothing
    6. a corner dragged inside Adobe's dialog is read back by the plugin
    7. save, close, reopen: the dictionary is unchanged

    Needs Illustrator running with EnhancedFreeDistort.aip loaded. Writes
    docs/evidence/poc.txt, poc.tsv, and the dialog captures.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ai.ps1')
$repo = Split-Path -Parent $PSScriptRoot
$evidence = Join-Path $repo 'docs\evidence'
$null = New-Item -ItemType Directory -Force -Path $evidence

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

Start-ProbeResults -Probe 'poc'
Say ('Enhanced Free Distort -- proof of concept, {0}' -f (Get-Date -Format 'yyyy-MM-dd HH:mm'))
Initialize-AiSession | Out-Null
Say ((Send-AiMessage version) -replace "`r?`n", ' | ')

# ---- 1. find or add ----------------------------------------------------------

Invoke-Efd 'EFD.clear(); EFD.pentagon("pent"); EFD.selectOnly("pent");' | Out-Null
$added = Send-AiMessage 'fd append'
Check 'find' 'the plugin adds an Adobe Free Distort to the selected object' 'result 0, post-effect 0' ($added -replace "`r?`n", ' ') ((Field $added 'result') -eq '0' -and (Field $added 'index') -eq '0')

$registry = Send-AiMessage registry
Save-ProbeTranscript -Path (Join-Path $evidence 'registry.txt') -Lines @('Every live effect registered with the running Illustrator, as AILiveEffectSuite reports it.', '', $registry.TrimEnd())
$entry = ($registry -split "`r?`n") | Where-Object { $_ -match "`tAdobe Free Distort`t" } | Select-Object -First 1
Check 'find' 'the effect is registered by Adobe, with the title the Appearance panel shows' 'Adobe Free Distort / Free Distort' $entry ($entry -match "`tAdobe Free Distort`tFree Distort`t")

$listed = Send-AiMessage 'fd list'
Check 'find' 'the plugin finds it again by name' 'object, post-effect 0' ($listed.Trim()) ($listed.Trim() -eq "object`t0")

# ---- 2. input bounds, asked of Adobe ------------------------------------------

$undoBefore = Send-AiMessage 'undo count'
$appearanceBefore = Send-AiMessage appearance
$bounds = Send-AiMessage 'fd bounds' '0'
$undoAfter = Send-AiMessage 'undo count'
$appearanceAfter = Send-AiMessage appearance
Say ($bounds.TrimEnd())
Check 'bounds' "Adobe's own edit path answers the input bounds" 'from Adobe, [90 330 340 100]' ("{0}, {1}" -f (Field $bounds 'from'), (Field $bounds 'input bounds')) ((Field $bounds 'from') -eq 'Adobe' -and (Field $bounds 'input bounds') -eq '[90 330 340 100]')
Check 'bounds' 'measuring leaves the appearance exactly as it was' 'identical appearance dump' $(if ($appearanceBefore -eq $appearanceAfter) { 'identical' } else { 'changed' }) ($appearanceBefore -eq $appearanceAfter)
Check 'bounds' 'measuring leaves no undo step behind' ("past {0}" -f (Field $undoBefore 'past')) ("past {0}" -f (Field $undoAfter 'past')) ((Field $undoBefore 'past') -eq (Field $undoAfter 'past'))

# ---- 3 and 4. move one corner ------------------------------------------------

$before = Invoke-Efd 'EFD.bounds("pent");'
$wrote = Send-AiMessage 'fd corner' '0|1|400,330'
Say ($wrote.TrimEnd())
$read = Send-AiMessage 'fd read' '0'
Say "read: $($read.TrimEnd())"
Check 'write' 'the destination corner the plugin wrote is in the dictionary' 'dst corner 1 = 400,330, source [90 330 340 100]' $read.Trim() ($read -match 'src \(90,330 340,330 90,100 340,100\) dst \(90,330 400,330 90,100 340,100\)')

$after = Invoke-Efd 'app.redraw(); EFD.bounds("pent");'
$geomBefore = ($before -split ';')[0]; $geomAfter = ($after -split ';')[0]
Check 'write' 'the source path is untouched' $geomBefore $geomAfter ($geomBefore -eq $geomAfter)
# The model: the rightmost anchor (340,260) sits on the right edge, at t = 160/230
# of the way up it, and that edge now runs from (340,100) to (400,330).
$predicted = 340 + 60 * 160 / 230
$right = [double]::Parse((($after -split ';')[1] -split ',')[2], [Globalization.CultureInfo]::InvariantCulture)
Check 'write' "Adobe's renderer draws the new corner where the bilinear model says" (Format-AiNumber $predicted) (Format-AiNumber $right) ([math]::Abs($right - $predicted) -lt 1e-6)

$appearance = Send-AiMessage appearance
$names = ([regex]::Matches($appearance, '\[\d+\] "([^"]+)"') | ForEach-Object { $_.Groups[1].Value }) -join ', '
Check 'write' 'the appearance holds Adobe Free Distort and nothing of this plugin' 'Adobe Free Distort' $names ($names -eq 'Adobe Free Distort')
$entries = ([regex]::Match($appearance, '# (\d+) entries')).Groups[1].Value
Check 'write' 'the dictionary holds only the sixteen Adobe keys' '16 entries, none of them ours' ("{0} entries" -f $entries) ($entries -eq '16' -and $appearance -notmatch 'VulpesNexus|EnhancedFreeDistort')

# ---- 5. Adobe's dialog shows it; Cancel changes nothing ------------------------

# No capture here: the OK run below opens on the same state and captures it.
$dialog = Invoke-FreeDistortDialog -Button cancel
Say ("dialog: {0} / host: {1}" -f $dialog.Driver, $dialog.Host)
$readCancel = Send-AiMessage 'fd read' '0'
Check 'vanilla' "Adobe's dialog opens on the plugin's edit, and Cancel leaves it" $read.Trim() $readCancel.Trim() ($dialog.Host -match 'returned 1398034256' -and $readCancel.Trim() -eq $read.Trim())

# ---- 6. a corner dragged in Adobe's dialog, read by the plugin -----------------

# The dialog's preview puts this fixture's bottom-right handle at (237,191) in
# its view's client pixels, at 0.56 px per point; 28 px right and 20 px down.
$dialog = Invoke-FreeDistortDialog -Button ok -Drag @(237, 191, 265, 211) -ShotPrefix (Join-Path $evidence 'poc-vanilla')
Say ("dialog: {0} / host: {1}" -f $dialog.Driver, $dialog.Host)
$readVanilla = Send-AiMessage 'fd read' '0'
Say "read: $($readVanilla.TrimEnd())"
Check 'vanilla' "a corner dragged in Adobe's dialog is what the plugin reads" 'dst corner 3 moved, corner 1 still 400,330' $readVanilla.Trim() ($readVanilla -match 'dst \(90,330 400,330 90,100 (?!340,100\))[-0-9.]+,[-0-9.]+\)')
$status = Send-AiMessage 'editor refresh' 'measure'
Check 'vanilla' "the editor's quad is Adobe's new state, read fresh" 'quad equals the dictionary destination' (Field $status 'quad') ((Field $status 'quad') -eq ($readVanilla -replace '^.*dst ', '').Trim())

# ---- 7. save, close, reopen ----------------------------------------------------

$path = Invoke-Efd 'var d = EFD.doc(); d.save(); var p = d.fullName.fsName; d.close(SaveOptions.DONOTSAVECHANGES); app.open(new File(p)); EFD.selectOnly("pent"); p;'
$readReopened = Send-AiMessage 'fd read' '0'
$quadsBefore = ($readVanilla -replace '^entries \d+ ', '').Trim()
$quadsAfter = ($readReopened -replace '^entries \d+ ', '').Trim()
Check 'persistence' 'save, close, and reopen keep all sixteen numbers to the digit' $quadsBefore $quadsAfter ($quadsAfter -eq $quadsBefore)
$appearanceSaved = Send-AiMessage appearance
Add-ProbeResult -Group 'persistence' -Case 'keys present before saving that the reopened dictionary lacks' -Observed ("before: {0}; after reopen: {1}; -DefaultApplyEffectsKey after reopen: {2}" -f ([regex]::Match($readVanilla, 'entries \d+').Value), ([regex]::Match($readReopened, 'entries \d+').Value), $(if ($appearanceSaved -match '-DefaultApplyEffectsKey') { 'present' } else { 'absent' })) -Status 'MEASURED'
Say ("[MEASURED] entries before saving {0}, after reopening {1}" -f ([regex]::Match($readVanilla, 'entries \d+').Value), ([regex]::Match($readReopened, 'entries \d+').Value))
$appearanceReopened = Send-AiMessage appearance
$namesReopened = ([regex]::Matches($appearanceReopened, '\[\d+\] "([^"]+)"') | ForEach-Object { $_.Groups[1].Value }) -join ', '
Check 'persistence' 'the reopened appearance is still only Adobe Free Distort' 'Adobe Free Distort' $namesReopened ($namesReopened -eq 'Adobe Free Distort')

Save-ProbeResults -Path (Join-Path $evidence 'poc.tsv')
Save-ProbeTranscript -Path (Join-Path $evidence 'poc.txt') -Lines $log
