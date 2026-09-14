<#
.SYNOPSIS
    Whether the editor being active makes Illustrator crash while documents
    are saved as PDF, closed, and opened again.

.DESCRIPTION
    Illustrator 30.7.0 dies with an access violation under repeated scripted
    document churn with no third-party plugin involved at all (LiveShear's
    crash-control evidence). A crash seen with this plugin's tool active is
    therefore not evidence against the plugin by itself, and not evidence for
    it either. This interleaves two arms over the same cycle:

      tool      the FreeDistort+ selected, its annotator drawing handles
      control   the Selection tool selected, the annotator inactive

    and records, cycle by cycle, whether Illustrator survived. Illustrator is
    restarted after a crash and the arm carries on.

    Writes docs/evidence/churn.txt and churn.tsv.
#>
[CmdletBinding()]
param([int] $CyclesPerArm = 8)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ai.ps1')
$repo = Split-Path -Parent $PSScriptRoot
$evidence = Join-Path $repo 'docs\evidence'
$scratch = Join-Path ([IO.Path]::GetTempPath()) 'fdp-probes'
$null = New-Item -ItemType Directory -Force -Path $scratch
$pdf = (Join-Path $scratch 'fdp-churn.pdf') -replace '\\', '/'

$log = New-Object Collections.Generic.List[string]
function Say([string] $s) { $log.Add($s); Write-Output $s }
Start-ProbeResults -Probe 'churn'
Say ('FreeDistort+ -- document churn A/B, {0}' -f (Get-Date -Format 'yyyy-MM-dd HH:mm'))

function Prepare([string] $arm) {
    Initialize-AiSession | Out-Null
    Invoke-Fdp 'FDP.clear(); FDP.pentagon("pent");' | Out-Null
    Invoke-Fdp 'FDP.selectOnly("pent"); app.redraw();' | Out-Null
    Send-AiMessage 'fd append' | Out-Null
    Send-AiMessage 'fd corner' '0|1|400,340' | Out-Null
    if ($arm -eq 'tool') { Send-AiMessage 'editor open' | Out-Null }
    else { Send-AiMessage 'tool select' 'Adobe Select Tool' | Out-Null }
    Invoke-Fdp 'FDP.doc().save(); "saved";' | Out-Null
}

$crashes = @{ tool = 0; control = 0 }
$survived = @{ tool = 0; control = 0 }
for ($cycle = 1; $cycle -le $CyclesPerArm; $cycle++) {
    foreach ($arm in 'tool', 'control') {
        $outcome = 'survived'
        try {
            Prepare $arm
            $active = (Send-AiMessage 'editor status') -split "`r?`n" | Where-Object { $_ -like "active`t*" }
            Invoke-Fdp ("(function(){{ var d = FDP.doc(); var o = new PDFSaveOptions(); o.preserveEditability = true; o.viewAfterSaving = false; d.saveAs(new File('{0}'), o); d.close(SaveOptions.DONOTSAVECHANGES); var r = app.open(new File('{0}')); app.redraw(); r.close(SaveOptions.DONOTSAVECHANGES); return 'cycled'; }})();" -f $pdf) | Out-Null
            Invoke-AiScript 'app.documents.length + "";' | Out-Null
            $survived[$arm]++
        }
        catch {
            $outcome = 'CRASHED: ' + ($_.Exception.Message -replace "`r?`n", ' ')
            $crashes[$arm]++
            Start-Sleep -Seconds 5
            if (Get-Process Illustrator -ErrorAction SilentlyContinue) { Stop-Process -Name Illustrator -Force; Start-Sleep -Seconds 3 }
            Start-Ai | Out-Null
        }
        Add-ProbeResult -Group $arm -Case ("cycle {0}" -f $cycle) -Observed ("{0}; {1}" -f $outcome, ($active -replace "`t", ' ')) -Status 'MEASURED'
        Say ("cycle {0} {1}: {2} ({3})" -f $cycle, $arm, $outcome, ($active -replace "`t", ' '))
    }
}
foreach ($arm in 'tool', 'control') {
    Add-ProbeResult -Group 'summary' -Case $arm -Observed ("{0} cycles survived, {1} crashed" -f $survived[$arm], $crashes[$arm]) -Status 'MEASURED'
    Say ("{0}: {1} survived, {2} crashed" -f $arm, $survived[$arm], $crashes[$arm])
}
Save-ProbeResults -Path (Join-Path $evidence 'churn.tsv')
Save-ProbeTranscript -Path (Join-Path $evidence 'churn.txt') -Lines $log
