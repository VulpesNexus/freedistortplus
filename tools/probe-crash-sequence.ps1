<#
.SYNOPSIS
    Replays the sequence Illustrator crashed in during these probes --
    copy and paste, Save As PDF, close, reopen, close, a new
    document -- with FreeDistort+'s tool active and with Illustrator's Selection
    tool active, and records which runs survive.

.DESCRIPTION
    Each run starts a fresh Illustrator. Every call is logged to a step file
    before it runs (FDP_STEPLOG), so a crash names the call it happened in, and
    the Windows Application log is read for Illustrator's fault offset.

    Needs FreeDistortPlus.aip installed. Writes docs/evidence/crash-sequence.txt
    and crash-sequence.tsv.
#>
[CmdletBinding()]
param([int] $Runs = 2, [string[]] $Tools = @('Adobe Select Tool', 'VulpesNexus FreeDistort+ Tool'),
      # edited: the plugin adds and drags a Free Distort first. passive: the plugin
      # is loaded but never called, and the art has no Free Distort. file: the art
      # comes from -Fixture, a saved document, and nothing calls the plugin.
      [ValidateSet('edited', 'passive', 'file')] [string] $Variant = 'edited', [string] $Fixture = '')

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ai.ps1')
$repo = Split-Path -Parent $PSScriptRoot
$evidence = Join-Path $repo 'docs\evidence'
$scratch = Join-Path ([IO.Path]::GetTempPath()) 'fdp-probes'
$null = New-Item -ItemType Directory -Force -Path $scratch
$pdf = (Join-Path $scratch 'fdp-crash-sequence.pdf') -replace '\\', '/'

$log = New-Object Collections.Generic.List[string]
function Say([string] $s) { $log.Add($s); Write-Output $s }

function WaitGone {
    $deadline = (Get-Date).AddMinutes(3)
    while ((Get-Process Illustrator -ErrorAction SilentlyContinue) -and (Get-Date) -lt $deadline) { Start-Sleep -Seconds 2 }
}

function Run([string] $tool, [int] $n) {
    if (Get-Process Illustrator -ErrorAction SilentlyContinue) { Stop-Ai | Out-Null }
    WaitGone
    Start-Ai | Out-Null
    $env:FDP_STEPLOG = Join-Path $scratch 'fdp-crash-steps.txt'
    [IO.File]::WriteAllText($env:FDP_STEPLOG, '')
    $since = Get-Date
    $step = 'start'
    try {
        $step = 'document'; Initialize-AiSession | Out-Null
        $step = 'fixture'
        if ($Variant -eq 'file') {
            Invoke-AiScript ("(function(){{ var src = app.open(new File('{0}')); src.selection = null; for (var i = 0; i < src.pageItems.length; i++) {{ src.pageItems[i].selected = true; }} app.copy(); src.close(SaveOptions.DONOTSAVECHANGES); return 'copied'; }})();" -f ($Fixture -replace '\\', '/')) | Out-Null
            Invoke-Fdp 'FDP.clear(); app.paste(); var d = FDP.doc(); d.selection[0].name = "pent"; FDP.selectOnly("pent"); "pasted";' | Out-Null
        } else {
            Invoke-Fdp 'FDP.clear(); FDP.pentagon("pent"); FDP.selectOnly("pent");' | Out-Null
        }
        if ($Variant -eq 'edited') {
            Send-AiMessage 'fd append' | Out-Null
            $step = 'tool'; if ($tool -like '*FreeDistort*') { Send-AiMessage 'editor open' | Out-Null } else { Send-AiMessage 'tool select' $tool | Out-Null }
            $step = 'drag'; Send-AiMessage 'editor drag' '1|free|-1|400,345' | Out-Null
        }
        $step = 'copy and paste'; Invoke-Fdp 'app.copy(); app.paste(); var d = FDP.doc(); var p = d.selection[0]; p.name = "pasted"; FDP.selectOnly("pasted"); app.redraw(); "pasted";' | Out-Null
        $step = 'save as PDF, close, reopen'
        Invoke-Fdp ("(function(){{ var d = FDP.doc(); FDP.selectOnly('pent'); var o = new PDFSaveOptions(); o.preserveEditability = true; o.viewAfterSaving = false; d.saveAs(new File('{0}'), o); d.close(SaveOptions.DONOTSAVECHANGES); var r = app.open(new File('{0}')); var p = null; for (var i = 0; i < r.pageItems.length; i++) {{ if (r.pageItems[i].name === 'pent') {{ p = r.pageItems[i]; }} }} r.selection = null; if (p) {{ p.selected = true; }} return 'reopened'; }})();" -f $pdf) | Out-Null
        if ($Variant -eq 'edited') { $step = 'read'; Send-AiMessage 'fd read' '0' | Out-Null }
        $step = 'close the PDF'; Invoke-AiScript "(function(){ for (var i = app.documents.length - 1; i >= 0; i--) { if (app.documents[i].name === 'fdp-crash-sequence.pdf') { app.documents[i].close(SaveOptions.DONOTSAVECHANGES); } } return 'closed'; })();" | Out-Null
        $step = 'new document'; Initialize-AiSession | Out-Null
        $step = 'after'; Start-Sleep -Seconds 2; Invoke-AiScript 'app.documents.length + "";' | Out-Null
        $outcome = 'survived'
    }
    catch {
        $outcome = "died during: $step"
    }
    $env:FDP_STEPLOG = ''
    Start-Sleep -Seconds 3
    $fault = Get-WinEvent -FilterHashtable @{ LogName = 'Application'; Id = 1000; StartTime = $since } -ErrorAction SilentlyContinue |
        Where-Object { $_.Message -match 'Illustrator\.exe' } | Select-Object -First 1
    $offset = if ($fault) { ([regex]::Match($fault.Message, 'Fault offset: (0x[0-9a-fA-F]+)')).Groups[1].Value } else { '' }
    $last = (Get-Content (Join-Path $scratch 'fdp-crash-steps.txt') -ErrorAction SilentlyContinue | Where-Object { $_ } | Select-Object -Last 1)
    $installed = Test-Path (Join-Path (Join-Path $env:LOCALAPPDATA 'Adobe Illustrator Plug-ins\30') 'FreeDistortPlus.aip')
    $outcome = "{0} (FreeDistortPlus.aip {1})" -f $outcome, $(if ($installed) { 'installed' } else { 'not installed' })
    $status = if ($outcome -like 'survived*') { 'PASS' } else { 'FAIL' }
    $observed = if ($offset) { "{0}; fault offset {1}; last call: {2}" -f $outcome, $offset, (Hide-Personal ([string] $last)) } else { $outcome }
    Add-ProbeResult -Group ("{0}, {1}" -f $Variant, $tool) -Case ("{0} run {1} with {2} active" -f $Variant, $n, $tool) -Expected 'survived' -Observed $observed -Status $status
    Say ("[{0}] {1} run {2}, {3}: {4}" -f $status, $Variant, $n, $tool, $observed)
}

Start-ProbeResults -Probe 'crash-sequence'
$previous = Join-Path $evidence 'crash-sequence.tsv'
if (Test-Path $previous) { foreach ($row in (Import-Csv -Path $previous -Delimiter "`t")) { Add-ProbeResult -Group $row.group -Case $row.case -Expected $row.expected -Observed $row.observed -Status $row.status } }
$transcript = Join-Path $evidence 'crash-sequence.txt'
if (Test-Path $transcript) { foreach ($l in [IO.File]::ReadAllLines($transcript)) { $log.Add($l) } }
Say ('FreeDistort+ -- the copy, PDF, close, reopen, close, new document sequence, variant {0}, {1}' -f $Variant, (Get-Date -Format 'yyyy-MM-dd HH:mm'))
for ($n = 1; $n -le $Runs; $n++) { foreach ($tool in $Tools) { Run $tool $n } }
if (Get-Process Illustrator -ErrorAction SilentlyContinue) { try { Stop-Ai | Out-Null } catch { } }
WaitGone
Start-Ai | Out-Null
Save-ProbeResults -Path (Join-Path $evidence 'crash-sequence.tsv')
Save-ProbeTranscript -Path (Join-Path $evidence 'crash-sequence.txt') -Lines $log
