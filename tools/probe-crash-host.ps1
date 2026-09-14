<#
.SYNOPSIS
    The document work of the probes that crashed Illustrator, in plain
    ExtendScript that never calls this plugin, with the plugin installed, with
    it removed, and with every third-party plugin removed.

.DESCRIPTION
    Each trial starts a fresh Illustrator. The work: an effect applied through
    Adobe's own Effect > Distort & Transform > Free Distort item (with alerts
    off it applies at once), the document saved, closed, and reopened; every
    art type with the effect, live text retyped; copy and paste, SVG export,
    Apply Last Effect; then Save As PDF with editing capabilities, close,
    reopen, a read of the art, and closing the PDF. Every call is logged before
    it runs, and faults are read from the Windows Application log.

    A crashing Illustrator stays alive for about half a minute while Windows
    writes its dump, so a fault is counted from the log, not from whether the
    process has exited yet.

    The plugin folder is put back as it was, whatever happens: arm "removed"
    takes FreeDistortPlus.aip out, and arm "bare" takes out every .aip in the
    folder. Needs Illustrator's Additional Plug-ins Folder set, and nobody else
    using Illustrator. Writes docs/evidence/crash-host.txt.
#>
[CmdletBinding()]
param(
    [int] $Rounds = 4,
    [string[]] $Arms = @('removed', 'installed')
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ai.ps1')
$repo = Split-Path -Parent $PSScriptRoot
$folder = Get-AiAdditionalPluginFolder
if (-not $folder) { throw "Illustrator's Additional Plug-ins Folder is not set." }
$target = Join-Path $folder 'FreeDistortPlus.aip'
$scratch = Join-Path ([IO.Path]::GetTempPath()) 'fdp-probes'
$held = Join-Path $scratch 'held-plugins'
$null = New-Item -ItemType Directory -Force -Path $held
$steps = Join-Path $scratch 'crash-host-steps.txt'
$tmp = $scratch -replace '\\', '/'

# Everything in the folder is copied aside first, so every arm can be restored exactly.
$originals = @(Get-ChildItem $folder -Filter *.aip | ForEach-Object { $_.Name })
foreach ($name in $originals) { [IO.File]::Copy((Join-Path $folder $name), (Join-Path $held $name), $true) }
if ($originals -notcontains 'FreeDistortPlus.aip') { throw 'FreeDistortPlus.aip is not installed.' }

$log = New-Object Collections.Generic.List[string]
function Say([string] $s) { $log.Add($s); Write-Output $s }
function WaitGone {
    $deadline = (Get-Date).AddMinutes(4)
    while ((Get-Process Illustrator -ErrorAction SilentlyContinue) -and (Get-Date) -lt $deadline) { Start-Sleep -Seconds 2 }
    if (Get-Process Illustrator -ErrorAction SilentlyContinue) { throw 'Illustrator did not exit within four minutes.' }
}
function Faults([datetime] $since) {
    @(Get-WinEvent -FilterHashtable @{ LogName = 'Application'; Id = 1000; StartTime = $since } -ErrorAction SilentlyContinue |
      Where-Object { $_.Message -match 'Illustrator\.exe' } |
      ForEach-Object { ([regex]::Match($_.Message, 'Fault offset: 0x0*([0-9a-fA-F]+)')).Groups[1].Value } |
      ForEach-Object { '0x' + $_.ToLower() })
}
function Apply { Invoke-AiScript 'app.executeMenuCommand("Live Free Distort"); app.redraw(); "applied";' | Out-Null }
function Set-Folder([string] $arm) {
    foreach ($name in $originals) {
        $path = Join-Path $folder $name
        $keep = ($arm -eq 'installed') -or ($arm -eq 'removed' -and $name -ne 'FreeDistortPlus.aip')
        if ($keep -and -not (Test-Path $path)) { [IO.File]::Copy((Join-Path $held $name), $path, $false) }
        if (-not $keep -and (Test-Path $path)) { [IO.File]::Delete($path) }
    }
}

function Invoke-DocumentWork {
    Initialize-AiSession | Out-Null
    Invoke-Fdp 'FDP.clear(); FDP.pentagon("pent"); FDP.selectOnly("pent");' | Out-Null
    Apply
    Invoke-AiScript '(function(){ var d = FDP.doc(); var f = d.fullName; d.save(); d.close(SaveOptions.DONOTSAVECHANGES); var r = app.open(f); return r.name; })();' | Out-Null
    Invoke-Fdp 'FDP.doc(); app.redraw(); "ready";' | Out-Null
    foreach ($fn in 'pentagon', 'compound', 'group', 'clipGroup', 'pointText', 'areaText', 'symbolInstance', 'raster') {
        Invoke-Fdp ("(function(){{ FDP.clear(); FDP.{0}('fx'); FDP.selectOnly('fx'); return 'made'; }})();" -f $fn) | Out-Null
        Invoke-Fdp 'app.redraw(); FDP.bounds("fx");' | Out-Null
        Apply
        Invoke-Fdp 'app.redraw(); FDP.bounds("fx");' | Out-Null
        if ($fn -like '*Text') { Invoke-Fdp "(function(){ var o = FDP.named('fx'); o.contents = o.contents + ' more'; app.redraw(); return 'retyped'; })();" | Out-Null }
    }
    Invoke-Fdp 'FDP.clear(); FDP.pentagon("pent"); FDP.selectOnly("pent");' | Out-Null
    Apply
    Invoke-Fdp 'app.copy(); app.paste(); var d = FDP.doc(); var p = d.selection[0]; p.name = "pasted"; FDP.selectOnly("pasted"); app.redraw(); "pasted";' | Out-Null
    Invoke-Fdp ("(function(){{ var d = FDP.doc(); d.exportFile(new File('{0}/crash-host.svg'), ExportType.SVG, new ExportOptionsSVG()); return 'svg'; }})();" -f $tmp) | Out-Null
    Invoke-Fdp 'FDP.clear(); FDP.pentagon("first"); FDP.pentagon("second", 300, 0); FDP.selectOnly("second");' | Out-Null
    Apply
    Invoke-AiScript 'app.executeMenuCommand("Adobe Apply Last Effect"); app.redraw(); "again";' | Out-Null
    foreach ($fn in 'pentagon', 'grid', 'areaText') {
        Invoke-Fdp ("FDP.clear(); FDP.{0}('cost');" -f $fn) | Out-Null
        Invoke-Fdp "FDP.selectOnly('cost'); app.redraw();" | Out-Null
        Apply
    }
    Invoke-Fdp 'FDP.clear(); FDP.pentagon("pent"); FDP.selectOnly("pent");' | Out-Null
    Apply
    Invoke-Fdp ("(function(){{ var d = FDP.doc(); FDP.selectOnly('pent'); var o = new PDFSaveOptions(); o.preserveEditability = true; o.viewAfterSaving = false; d.saveAs(new File('{0}/crash-host.pdf'), o); d.close(SaveOptions.DONOTSAVECHANGES); var r = app.open(new File('{0}/crash-host.pdf')); var p = null; for (var i = 0; i < r.pageItems.length; i++) {{ if (r.pageItems[i].name === 'pent') {{ p = r.pageItems[i]; }} }} r.selection = null; if (p) {{ p.selected = true; }} return r.name; }})();" -f $tmp) | Out-Null
    Invoke-AiScript '(function(){ var d = app.activeDocument; var p = d.selection[0]; app.redraw(); return p ? p.typename + " " + p.visibleBounds.join(",") : "none"; })();' | Out-Null
    Invoke-AiScript "(function(){ for (var i = app.documents.length - 1; i >= 0; i--) { if (app.documents[i].name === 'crash-host.pdf') { app.documents[i].close(SaveOptions.DONOTSAVECHANGES); } } return 'closed'; })();" | Out-Null
}

Say ('FreeDistort+ -- the crashing probes'' document work, with no call to this plugin, {0}' -f (Get-Date -Format 'yyyy-MM-dd HH:mm'))
Say 'Arms: installed = every plugin in the folder; removed = FreeDistortPlus.aip taken out; bare = no third-party plugin at all.'
$tally = @{}
try {
    for ($round = 1; $round -le $Rounds; $round++) {
        foreach ($arm in $Arms) {
            if (Get-Process Illustrator -ErrorAction SilentlyContinue) { Stop-Ai | Out-Null }
            WaitGone
            Set-Folder $arm
            $present = @(Get-ChildItem $folder -Filter *.aip | ForEach-Object { $_.BaseName }) -join ', '
            Start-Sleep -Seconds 3
            $since = Get-Date
            Start-Ai | Out-Null
            [IO.File]::WriteAllText($steps, '')
            $env:FDP_STEPLOG = $steps
            $threw = ''
            try { Invoke-DocumentWork } catch { $threw = [string] $_ }
            $env:FDP_STEPLOG = ''
            # Long enough for a fault right after the last call to reach the log.
            Start-Sleep -Seconds 15
            try { if (Get-Process Illustrator -ErrorAction SilentlyContinue) { Stop-Ai | Out-Null } } catch { }
            WaitGone
            $faults = @(Faults $since)
            $last = if ($faults.Count) { (@([IO.File]::ReadAllLines($steps) | Where-Object { $_ }) | Select-Object -Last 1) -replace '^\S+\s+', '' } else { '' }
            if ($last.Length -gt 90) { $last = $last.Substring(0, 90) + '...' }
            $outcome = if ($faults.Count) { 'crashed at ' + ($faults -join ', ') + $(if ($last) { '; last call: ' + $last } else { '' }) } elseif ($threw) { 'no fault recorded, but a call failed: ' + $threw } else { 'survived' }
            if (-not $tally.ContainsKey($arm)) { $tally[$arm] = @(0, 0) }
            $tally[$arm] = @(($tally[$arm][0] + 1), ($tally[$arm][1] + [int] ($faults.Count -gt 0)))
            Say (Hide-Personal ("round {0}, {1} [{2}]: {3}" -f $round, $arm, $present, $outcome))
        }
    }
}
finally {
    $env:FDP_STEPLOG = ''
    if (Get-Process Illustrator -ErrorAction SilentlyContinue) { try { Stop-Ai | Out-Null } catch { } }
    try { WaitGone } catch { }
    Set-Folder 'installed'
    Start-Ai | Out-Null
}
foreach ($arm in $Arms) { if ($tally.ContainsKey($arm)) { Say ("{0}: {1} crashed of {2}" -f $arm, $tally[$arm][1], $tally[$arm][0]) } }
Save-ProbeTranscript -Path (Join-Path $repo 'docs\evidence\crash-host.txt') -Lines $log
