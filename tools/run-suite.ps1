<#
.SYNOPSIS
    Runs every host probe in order, then the solvers and the support matrix,
    and prints a count of results per probe.

.DESCRIPTION
    The release gate's host half. Each probe writes its own evidence; this
    only sequences them and reports. Probes that restart Illustrator run last.
    A probe that throws is reported and the suite moves on, so one failure
    does not hide the rest.

    Takes an hour or more, so start it detached:

        .\tools\run-detached.ps1 -Probe run-suite.ps1

    Needs Illustrator running with FreeDistortPlus.aip installed. The checks a
    person makes (record-manual-drag.ps1, record-manual-checks.ps1,
    record-free-transform.ps1) are not part of it.
#>
[CmdletBinding()]
param(
    [string[]] $Skip = @()
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ai.ps1')
$repo = Split-Path -Parent $PSScriptRoot
$evidence = Join-Path $repo 'docs\evidence'
$env:PYTHONIOENCODING = 'utf-8'

$probes = @(
    @{ Name = 'probe-poc.ps1'; Args = @{} },
    @{ Name = 'probe-mapping.ps1'; Args = @{} },
    @{ Name = 'probe-semantics.ps1'; Args = @{} },
    @{ Name = 'probe-editor.ps1'; Args = @{} },
    @{ Name = 'probe-preview.ps1'; Args = @{} },
    @{ Name = 'probe-support.ps1'; Args = @{} },
    @{ Name = 'probe-persistence.ps1'; Args = @{} },
    @{ Name = 'probe-source-quads.ps1'; Args = @{} },
    @{ Name = 'probe-source-follow.ps1'; Args = @{} },
    @{ Name = 'probe-numeric.ps1'; Args = @{} },
    @{ Name = 'probe-snap.ps1'; Args = @{} },
    @{ Name = 'probe-lifecycle.ps1'; Args = @{} },
    @{ Name = 'probe-churn.ps1'; Args = @{} },
    @{ Name = 'probe-missing-plugin.ps1'; Args = @{ Phase = 'all' } }
)

$started = Get-Date
foreach ($p in $probes) {
    if ($Skip -contains $p.Name) { Write-Output ("skip {0}" -f $p.Name); continue }
    $t0 = Get-Date
    try {
        # After a crash the dying process can linger while its dump is
        # written, answering nothing; wait it out, then start afresh.
        $deadline = (Get-Date).AddMinutes(3)
        while ((Get-Process Illustrator -ErrorAction SilentlyContinue) -and -not (Wait-AiReady -TimeoutSeconds 20) -and (Get-Date) -lt $deadline) { Start-Sleep -Seconds 5 }
        if (-not (Get-Process Illustrator -ErrorAction SilentlyContinue)) { Start-Ai | Out-Null }
        $named = $p.Args
        & (Join-Path $PSScriptRoot $p.Name) @named *> $null
        Write-Output ("done {0} in {1:n0} s" -f $p.Name, ((Get-Date) - $t0).TotalSeconds)
    }
    catch {
        Write-Output ("THREW {0}: {1}" -f $p.Name, (Hide-Personal ([string] $_)))
    }
}

& (Join-Path $PSScriptRoot 'run-mathtest.ps1') *> $null
python (Join-Path $PSScriptRoot 'solve-mapping.py') | Out-Null
python (Join-Path $PSScriptRoot 'solve-source-quads.py') | Out-Null
python (Join-Path $PSScriptRoot 'solve-free-transform.py') | Out-Null
python (Join-Path $PSScriptRoot 'make-support-matrix.py') | Out-Null

Write-Output ''
foreach ($file in Get-ChildItem (Join-Path $evidence '*.tsv')) {
    $rows = Import-Csv -Path $file.FullName -Delimiter "`t"
    if (-not $rows -or -not ($rows[0].PSObject.Properties.Name -contains 'probe')) { continue }
    $counts = $rows | Group-Object status | ForEach-Object { '{0} {1}' -f $_.Count, $_.Name }
    Write-Output ("{0,-22} {1}" -f $file.BaseName, ($counts -join ', '))
}
Write-Output ("suite finished in {0:n0} min" -f ((Get-Date) - $started).TotalMinutes)
