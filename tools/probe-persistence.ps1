<#
.SYNOPSIS
    What happens to an edited Free Distort on the way out of the document and
    back: copy and paste, PDF and SVG, and the menu command's neighbors. Also
    what a drag step costs.

.DESCRIPTION
    Needs Illustrator running with FreeDistortPlus.aip loaded. Exported
    files are written to the temp folder, never into the repository: a PDF
    carries creator metadata. Writes docs/evidence/persistence.txt and
    persistence.tsv.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ai.ps1')
$repo = Split-Path -Parent $PSScriptRoot
$evidence = Join-Path $repo 'docs\evidence'
$scratch = Join-Path ([IO.Path]::GetTempPath()) 'fdp-probes'
$null = New-Item -ItemType Directory -Force -Path $scratch
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
function Quads { ((Send-AiMessage 'fd read' '0') -replace '^entries \d+ ', '').Trim() }
function Count-FreeDistort { $l = (Send-AiMessage 'fd list').Trim(); if ($l -eq 'object') { 0 } else { ($l -split "`t").Count - 1 } }

Start-ProbeResults -Probe 'persistence'
Say ('FreeDistort+ -- persistence and cost, {0}' -f (Get-Date -Format 'yyyy-MM-dd HH:mm'))
Initialize-AiSession | Out-Null

Invoke-Fdp 'FDP.clear(); FDP.pentagon("pent"); FDP.selectOnly("pent");' | Out-Null
Send-AiMessage 'fd append' | Out-Null
Send-AiMessage 'editor refresh' 'measure' | Out-Null
Send-AiMessage 'editor drag' '1|free|-1|400,345' | Out-Null
Send-AiMessage 'editor drag' '2|free|-1|70,85' | Out-Null
$edited = Quads
$drawn = Invoke-Fdp 'app.redraw(); FDP.bounds("pent");'
Say "edited: $edited; drawn $drawn"

# ---- copy and paste --------------------------------------------------------------------------

Invoke-Fdp 'app.copy(); app.paste(); var d = FDP.doc(); var p = d.selection[0]; p.name = "pasted"; FDP.selectOnly("pasted"); app.redraw(); "pasted";' | Out-Null
$pasted = Quads
Check 'clipboard' 'copy and paste carry the edited Free Distort, all sixteen numbers' $edited $pasted ($pasted -eq $edited)
$pastedDrawn = Invoke-Fdp 'FDP.bounds("pasted");'
Record 'clipboard' 'where the pasted copy draws, against the original' ("original {0}; pasted {1}" -f $drawn, $pastedDrawn)

# ---- SVG ----------------------------------------------------------------------------------------

$svg = Join-Path $scratch 'fdp-persistence.svg'
Invoke-Fdp ("(function(){{ var d = FDP.doc(); var o = new ExportOptionsSVG(); d.exportFile(new File('{0}'), ExportType.SVG, o); return 'svg'; }})();" -f ($svg -replace '\\', '/')) | Out-Null
$svgText = if (Test-Path $svg) { [IO.File]::ReadAllText($svg) } else { '' }
Check 'export' 'SVG export writes the distorted drawing' 'an SVG with a polygon or path' ("{0} bytes" -f $svgText.Length) ($svgText -match '<(path|polygon)')
Check 'export' 'the SVG names no part of this plugin' 'none' $(if ($svgText -match 'FreeDistortPlus|VulpesNexus') { 'found' } else { 'none' }) (-not ($svgText -match 'FreeDistortPlus|VulpesNexus'))

# ---- the menu command, and Apply Last Effect beside it ------------------------------------------------

Record 'menu' 'where the command was placed' (Field (Send-AiMessage 'menu') 'placement')
Invoke-Fdp 'FDP.clear(); FDP.pentagon("first"); FDP.pentagon("second", 300, 0); FDP.selectOnly("first");' | Out-Null
Send-AiMessage 'tool select' 'Adobe Select Tool' | Out-Null
Invoke-AiScript 'app.executeMenuCommand("VulpesNexus FreeDistort+"); "invoked";' | Out-Null
$status = Send-AiMessage 'editor status'
Check 'menu' 'the command, invoked by its command string, adds Free Distort and selects the editor' 'active, target valid' ("{0}, {1}" -f (Field $status 'active'), (Field $status 'target')) ((Field $status 'active') -eq 'yes' -and (Field $status 'target') -eq 'valid')

Invoke-Fdp 'FDP.selectOnly("second");' | Out-Null
$before = Count-FreeDistort
Invoke-AiScript 'app.executeMenuCommand("Live Free Distort"); app.redraw(); "applied";' | Out-Null
$afterMenu = Count-FreeDistort
Invoke-AiScript 'app.executeMenuCommand("Adobe Apply Last Effect"); app.redraw(); "again";' | Out-Null
$afterLast = Count-FreeDistort
Check 'menu' "Adobe's own Free Distort item still applies, and Apply Last Effect repeats it, with this command in the same submenu" 'counts 0, 1, 2' ("{0}, {1}, {2}" -f $before, $afterMenu, $afterLast) ($before -eq 0 -and $afterMenu -eq 1 -and $afterLast -eq 2)

# ---- cost of a drag step ---------------------------------------------------------------------------------

function Time-Drag([string] $fixture, [string] $name, [int] $steps) {
    Invoke-Fdp ("FDP.clear(); FDP.{0}('{1}');" -f $fixture, $name) | Out-Null
    # Selected in a call of its own: text selected in the call that created it
    # is not yet selected as far as a plugin is concerned.
    Invoke-Fdp ("FDP.selectOnly('{0}'); app.redraw();" -f $name) | Out-Null
    Send-AiMessage 'fd append' | Out-Null
    $status = Send-AiMessage 'editor refresh' 'measure'
    $n = [regex]::Matches((Field $status 'quad'), '-?[0-9.]+(e-?\d+)?') | ForEach-Object { [double]::Parse($_.Value, $inv) }
    $path = (1..$steps | ForEach-Object { '{0},{1}' -f (Format-AiNumber ($n[2] + 40 * [math]::Sin($_ / 7.0))), (Format-AiNumber ($n[3] + 25 * [math]::Cos($_ / 5.0))) }) -join ';'
    $watch = [Diagnostics.Stopwatch]::StartNew()
    Send-AiMessage 'editor drag' "1|free|-1|$path" | Out-Null
    $watch.Stop()
    $drag = $watch.Elapsed.TotalMilliseconds
    $watch = [Diagnostics.Stopwatch]::StartNew()
    Invoke-AiScript 'app.redraw(); "drawn";' | Out-Null
    $watch.Stop()
    # A drag step writes the dictionary; Illustrator runs the effect again when it
    # next draws, so the two costs are reported apart. In a real drag the host
    # draws between mouse events.
    Record 'cost' ("{0} drag steps on {1}" -f $steps, $fixture) ("{0} ms per dictionary write (all {1} steps in one call, round trip included); redrawing the result {2} ms" -f ($drag / $steps).ToString('F2', $inv), $steps, $watch.Elapsed.TotalMilliseconds.ToString('F0', $inv))
}
Time-Drag 'pentagon' 'cost' 200
Time-Drag 'grid' 'cost' 200
Time-Drag 'areaText' 'cost' 200

# ---- PDF with Illustrator editing capabilities, and back ----------------------------------------

Invoke-Fdp 'FDP.clear(); FDP.pentagon("pent"); FDP.selectOnly("pent");' | Out-Null
Send-AiMessage 'fd append' | Out-Null
Send-AiMessage 'editor refresh' 'measure' | Out-Null
Send-AiMessage 'editor drag' '1|free|-1|400,345' | Out-Null
Send-AiMessage 'editor drag' '2|free|-1|70,85' | Out-Null

$pdf = Join-Path $scratch 'fdp-persistence.pdf'
$pdfResult = Invoke-Fdp ("(function(){{ var d = FDP.doc(); FDP.selectOnly('pent'); var o = new PDFSaveOptions(); o.preserveEditability = true; o.viewAfterSaving = false; d.saveAs(new File('{0}'), o); d.close(SaveOptions.DONOTSAVECHANGES); var r = app.open(new File('{0}')); app.coordinateSystem = CoordinateSystem.DOCUMENTCOORDINATESYSTEM; var p = null; for (var i = 0; i < r.pageItems.length; i++) {{ if (r.pageItems[i].name === 'pent') {{ p = r.pageItems[i]; }} }} r.selection = null; if (p) {{ p.selected = true; }} return r.name + '|' + (p ? 'found' : 'missing'); }})();" -f ($pdf -replace '\\', '/'))
Say "pdf: $pdfResult"
$fromPdf = Quads
Check 'export' 'a PDF saved with Illustrator editing capabilities reopens with the same Free Distort' $edited $fromPdf ($fromPdf -eq $edited)
$pdfBytes = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($pdf))
Check 'export' 'the PDF names no part of this plugin' 'none' $(if ($pdfBytes -match 'FreeDistortPlus|VulpesNexus') { 'found' } else { 'none' }) (-not ($pdfBytes -match 'FreeDistortPlus|VulpesNexus'))
# Last, and nothing after it but closing the PDF: Illustrator 30.7.0 can crash
# creating a document after documents were closed under scripting, with or
# without this plugin (docs/evidence/crash-sequence.txt).
Invoke-AiScript "(function(){ for (var i = app.documents.length - 1; i >= 0; i--) { if (app.documents[i].name === 'fdp-persistence.pdf') { app.documents[i].close(SaveOptions.DONOTSAVECHANGES); } } return 'closed'; })();" | Out-Null

Save-ProbeResults -Path (Join-Path $evidence 'persistence.tsv')
Save-ProbeTranscript -Path (Join-Path $evidence 'persistence.txt') -Lines $log
