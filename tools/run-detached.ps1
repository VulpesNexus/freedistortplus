<#
.SYNOPSIS
    Starts a probe as a process of its own, detached from the caller, with its
    output going to a log file.

.DESCRIPTION
    A probe that restarts Illustrator runs for many minutes. Run as a child of
    an agent's shell, it dies with that shell; on a machine short of memory
    that shell can be killed partway, leaving Illustrator without a plugin the
    probe had taken out. A detached process finishes regardless.

    Prints the process id and the log path. The log is written through the
    probe's own output, so run the probe's evidence through Hide-Personal as
    usual; the log itself lives in the temp folder and is never committed.

.EXAMPLE
    .\tools\run-detached.ps1 -Probe probe-missing-plugin.ps1 -Arguments '-Phase all'
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $Probe,
    [string] $Arguments = ''
)

$ErrorActionPreference = 'Stop'
$script = Join-Path $PSScriptRoot $Probe
if (-not (Test-Path $script)) { throw "No such probe: $Probe" }

$logs = Join-Path ([IO.Path]::GetTempPath()) 'fdp-probes'
$null = New-Item -ItemType Directory -Force -Path $logs
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$base = Join-Path $logs ("{0}-{1}" -f [IO.Path]::GetFileNameWithoutExtension($Probe), $stamp)

$command = "& '{0}' {1} *> '{2}.log'; exit `$LASTEXITCODE" -f $script.Replace("'", "''"), $Arguments, $base.Replace("'", "''")
$process = Start-Process -FilePath 'powershell.exe' -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-Command', $command) -WindowStyle Hidden -PassThru
[pscustomobject]@{ Pid = $process.Id; Log = "$base.log" }
