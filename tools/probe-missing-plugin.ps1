<#
.SYNOPSIS
    The release invariant: a document edited with this plugin is an ordinary
    Adobe Free Distort document to an Illustrator without it, and whatever
    Adobe's own dialog does to it there is what this plugin reads afterwards.

.DESCRIPTION
    Three phases, each needing a different Illustrator:

      author   plugin loaded: edit Free Distort through the editor on a path, a
               point text, and a group; save fdp-missing.ai
      absent   plugin NOT loaded: open it and watch for any alert; check the
               drawing is unchanged; drag a corner in Adobe's own dialog; save
      return   plugin loaded again: read what Adobe's dialog wrote

    -Phase all runs the three in order, quitting Illustrator, taking the plugin
    out of the Additional Plug-ins Folder, and putting it back between them.
    Only run it when nothing else is using Illustrator.

    Without this plugin there is nothing of ours to open Adobe's editor with,
    so the absent phase opens it through LiveShear's bridge when LiveShear is
    installed: an independent plugin calling the same public
    AIArtStyleParserSuite::EditEffectParameters an Appearance-panel double-click
    uses. Without LiveShear that step is recorded as not run.

    Writes docs/evidence/missing-plugin.txt and missing-plugin.tsv.
#>
[CmdletBinding()]
param([ValidateSet('author', 'absent', 'return', 'all')] [string] $Phase = 'all')

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ai.ps1')
$repo = Split-Path -Parent $PSScriptRoot
$evidence = Join-Path $repo 'docs\evidence'
$state = Join-Path ([IO.Path]::GetTempPath()) 'fdp-probes'
$null = New-Item -ItemType Directory -Force -Path $state
$docPath = Join-Path $state 'fdp-missing.ai'
$inv = [Globalization.CultureInfo]::InvariantCulture

$transcript = Join-Path $evidence 'missing-plugin.txt'
$results = Join-Path $evidence 'missing-plugin.tsv'
$log = New-Object Collections.Generic.List[string]
if ($Phase -ne 'author' -and $Phase -ne 'all' -and (Test-Path $transcript)) { foreach ($l in [IO.File]::ReadAllLines($transcript)) { $log.Add($l) } }
Start-ProbeResults -Probe 'missing-plugin'
if ($Phase -ne 'author' -and $Phase -ne 'all' -and (Test-Path $results)) {
    foreach ($l in ([IO.File]::ReadAllLines($results) | Select-Object -Skip 1)) { $script:ProbeRows.Add($l) }
}

function Say([string] $s) { $log.Add($s); Write-Output $s }
function Check([string] $group, [string] $case, [string] $expected, [string] $observed, [bool] $ok) {
    $status = if ($ok) { 'PASS' } else { 'FAIL' }
    Add-ProbeResult -Group $group -Case $case -Expected $expected -Observed $observed -Status $status
    Say ("[{0}] {1}: {2}" -f $status, $case, $observed)
}
function Save-All {
    Save-ProbeResults -Path $results
    Save-ProbeTranscript -Path $transcript -Lines $log
}
function Open-Doc {
    Invoke-AiScript ("(function(){{ for (var i = 0; i < app.documents.length; i++) {{ if (app.documents[i].name === 'fdp-missing.ai') {{ app.documents[i].close(SaveOptions.DONOTSAVECHANGES); }} }} app.open(new File('{0}')); app.coordinateSystem = CoordinateSystem.DOCUMENTCOORDINATESYSTEM; return app.activeDocument.name; }})();" -f ($docPath -replace '\\', '/'))
}
function Select-Named([string] $name) {
    Invoke-AiScript ("(function(){{ var d = app.activeDocument; d.selection = null; for (var i = 0; i < d.pageItems.length; i++) {{ if (d.pageItems[i].name === '{0}') {{ d.pageItems[i].selected = true; return 'selected'; }} }} return 'missing'; }})();" -f $name)
}
function Drawing {
    Invoke-AiScript "(function(){ app.redraw(); var d = app.activeDocument, s = []; for (var i = 0; i < d.pageItems.length; i++) { var o = d.pageItems[i]; if (!o.name) { continue; } var v = o.visibleBounds; s.push(o.name + ':' + [Math.round(v[0]*1e6)/1e6, Math.round(v[1]*1e6)/1e6, Math.round(v[2]*1e6)/1e6, Math.round(v[3]*1e6)/1e6].join(',')); } s.sort(); return s.join(' '); })();"
}
function Loaded([string] $plugin) {
    try { return [bool] (Invoke-AiScript ("(function(){{ try {{ var r = app.sendScriptMessage('{0}', 'version', ''); return r ? 'yes' : ''; }} catch (e) {{ return ''; }} }})();" -f $plugin)) } catch { return $false }
}

function Phase-Author {
    Say ('-- author, {0}' -f (Get-Date -Format 'yyyy-MM-dd HH:mm'))
    Initialize-AiSession | Out-Null
    Check 'author' 'the plugin is loaded' 'loaded' $(if (Loaded 'FreeDistortPlus') { 'loaded' } else { 'absent' }) (Loaded 'FreeDistortPlus')
    Invoke-Fdp ("(function(){{ var d = app.documents.add(DocumentColorSpace.RGB, 800, 600); d.saveAs(new File('{0}')); app.coordinateSystem = CoordinateSystem.DOCUMENTCOORDINATESYSTEM; FDP.DOC_NAME = 'fdp-missing.ai'; FDP.pentagon('path'); FDP.pointText('text'); var a = FDP.pentagon('g1', 450, 0); var b = FDP.pentagon('g2', 520, 80); var g = d.groupItems.add(); FDP.named('g2').move(g, ElementPlacement.PLACEATEND); FDP.named('g1').move(g, ElementPlacement.PLACEATEND); g.name = 'group'; return 'built'; }})();" -f ($docPath -replace '\\', '/')) | Out-Null
    # The path gets the same corner as probe-poc.ps1, so Adobe's dialog shows
    # the same preview and its handles sit at the same pixels in the absent phase.
    $drags = @{ path = '1|free|-1|400,330'; text = '3|free|-1|NaN'; group = '0|converging|-1|NaN' }
    foreach ($name in 'path', 'text', 'group') {
        Select-Named $name | Out-Null
        Send-AiMessage 'fd append' | Out-Null
        $status = Send-AiMessage 'editor refresh' 'measure'
        $quad = ([regex]::Matches(($status -split "`r?`n" | Where-Object { $_ -like "quad`t*" }), '-?[0-9.]+') | ForEach-Object { [double]::Parse($_.Value, $inv) })
        $arg = $drags[$name]
        if ($arg -match 'NaN') {
            $corner = [int] $arg.Substring(0, 1)
            $to = '{0},{1}' -f (Format-AiNumber ($quad[$corner * 2] + 35)), (Format-AiNumber ($quad[$corner * 2 + 1] - 25))
            $arg = $arg -replace 'NaN', $to
        }
        $drag = Send-AiMessage 'editor drag' $arg
        Say ("{0}: {1}" -f $name, (($drag -split "`r?`n" | Where-Object { $_ -like 'end quad*' }) -join ''))
        Say ("{0}: {1}" -f $name, (Send-AiMessage 'fd read' '0').Trim())
    }
    $drawing = Drawing
    [IO.File]::WriteAllText((Join-Path $state 'drawing-author.txt'), $drawing)
    Say "drawing: $drawing"
    # Point the fixture library back at its own document before anything else
    # in this session uses it.
    Invoke-AiScript 'app.activeDocument.save(); app.activeDocument.close(SaveOptions.DONOTSAVECHANGES); FDP.DOC_NAME = "fdp-probe.ai"; "saved";' | Out-Null
    $bytes =[Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($docPath))
    Check 'author' 'the saved file names no part of this plugin' 'no FreeDistortPlus or VulpesNexus in the file' $(if ($bytes -match 'FreeDistortPlus|VulpesNexus') { 'found' } else { 'none' }) (-not ($bytes -match 'FreeDistortPlus|VulpesNexus'))
}

function Phase-Absent {
    Say ('-- absent, {0}' -f (Get-Date -Format 'yyyy-MM-dd HH:mm'))
    Initialize-AiSession | Out-Null
    Check 'absent' 'the plugin is not loaded' 'absent' $(if (Loaded 'FreeDistortPlus') { 'loaded' } else { 'absent' }) (-not (Loaded 'FreeDistortPlus'))

    $watch = Start-Job -FilePath (Join-Path $PSScriptRoot 'watch-alerts.ps1') -ArgumentList 25, (Join-Path $evidence 'missing-plugin-alert')
    Start-Sleep -Milliseconds 1500
    Open-Doc | Out-Null
    Start-Sleep -Seconds 3
    Stop-Job $watch | Out-Null
    $watched = @(Receive-Job $watch)
    Remove-Job $watch -Force
    $alerts = ($watched | Where-Object { $_ -like 'alert *:*' }) -join '; '
    Check 'absent' 'opening the document raises no alert at all' 'no alert' $(if ($alerts) { $alerts } else { 'no alert' }) (-not $alerts)

    $drawing = Drawing
    $authored = [IO.File]::ReadAllText((Join-Path $state 'drawing-author.txt'))
    Check 'absent' 'the drawing is exactly what was saved' $authored $drawing ($drawing -eq $authored)

    if (Loaded 'LiveShear') {
        Select-Named 'path' | Out-Null
        $appearance = Invoke-AiScript "app.sendScriptMessage('LiveShear', 'appearance', '');"
        $names = ([regex]::Matches($appearance, '\[\d+\] "([^"]+)"') | ForEach-Object { $_.Groups[1].Value }) -join ', '
        Check 'absent' 'the appearance, read by another plugin, is Adobe Free Distort' 'Adobe Free Distort' $names ($names -eq 'Adobe Free Distort')
        # Adobe's dialog, opened the way a double-click opens it. The handle
        # positions are this fixture's after the author phase's drag.
        $driver = Start-Job -FilePath (Join-Path $PSScriptRoot 'dialog-driver.ps1') -ArgumentList 30, 'ok', (Join-Path $evidence 'missing-plugin-vanilla'), @(237, 191, 265, 211)
        Start-Sleep -Milliseconds 1500
        Invoke-AiScript 'app.userInteractionLevel = UserInteractionLevel.DISPLAYALERTS; "on";' | Out-Null
        try { $opened = Invoke-AiScript "app.sendScriptMessage('LiveShear', 'edit effect', '0');" }
        finally { Invoke-AiScript 'app.userInteractionLevel = UserInteractionLevel.DONTDISPLAYALERTS; "off";' | Out-Null }
        $drove = @(Receive-Job $driver -Wait) -join ' | '
        Remove-Job $driver -Force
        Say "vanilla dialog: $drove / $($opened.Trim())"
        $after = Invoke-AiScript "app.sendScriptMessage('LiveShear', 'appearance', '');"
        [IO.File]::WriteAllText((Join-Path $state 'vanilla-appearance.txt'), $after)
        Check 'absent' "Adobe's own dialog edits the effect with the plugin absent" 'OK commits a moved corner' $drove ($opened -match 'returned 0' -and $drove -match 'dragged')
    }
    else {
        Add-ProbeResult -Group 'absent' -Case "Adobe's own dialog edits the effect with the plugin absent" -Expected 'an opener' -Observed 'LiveShear not installed; not run' -Status 'NOT RUN'
        Say '[NOT RUN] no plugin available to open the dialog'
    }
    Invoke-AiScript 'app.activeDocument.save(); app.activeDocument.close(SaveOptions.DONOTSAVECHANGES); "saved";' | Out-Null
}

function Phase-Return {
    Say ('-- return, {0}' -f (Get-Date -Format 'yyyy-MM-dd HH:mm'))
    Initialize-AiSession | Out-Null
    Check 'return' 'the plugin is loaded again' 'loaded' $(if (Loaded 'FreeDistortPlus') { 'loaded' } else { 'absent' }) (Loaded 'FreeDistortPlus')
    Open-Doc | Out-Null
    Select-Named 'path' | Out-Null
    $ours = (Send-AiMessage 'fd read' '0').Trim()
    Say "plugin reads: $ours"
    $vanillaFile = Join-Path $state 'vanilla-appearance.txt'
    if (Test-Path $vanillaFile) {
        $vanilla = [IO.File]::ReadAllText($vanillaFile)
        $want = foreach ($i in 0..3) { foreach ($axis in 'h', 'v') { [double]::Parse([regex]::Match($vanilla, "dst$i$axis \(Real\) = ([-0-9.]+)").Groups[1].Value, $inv) } }
        $got = [regex]::Matches(($ours -replace '^.*dst ', ''), '-?[0-9.]+') | ForEach-Object { [double]::Parse($_.Value, $inv) }
        $worst = 0.0
        for ($i = 0; $i -lt 8; $i++) { $worst = [math]::Max($worst, [math]::Abs($want[$i] - $got[$i])) }
        Check 'return' "the plugin reads exactly what Adobe's dialog wrote while it was absent" ("dst ({0})" -f (($want | ForEach-Object { Format-AiNumber $_ }) -join ',')) ("{0}; worst difference {1}" -f $ours, (Format-AiNumber $worst)) ($got.Count -eq 8 -and $worst -lt 1e-5)
    }
    $status = Send-AiMessage 'editor refresh' 'measure'
    $quad = ($status -split "`r?`n" | Where-Object { $_ -like "quad`t*" }) -replace "^quad`t", ''
    Check 'return' 'the editor shows that state, with no conversion' ($ours -replace '^.*dst ', '') $quad ($quad -eq ($ours -replace '^.*dst ', ''))
    Invoke-AiScript 'app.activeDocument.close(SaveOptions.DONOTSAVECHANGES); "closed";' | Out-Null
}

switch ($Phase) {
    'author' { Phase-Author }
    'absent' { Phase-Absent }
    'return' { Phase-Return }
    'all' {
        Phase-Author
        Save-All
        Stop-Ai | Out-Null
        & (Join-Path $PSScriptRoot 'install.ps1') -Uninstall | Out-Null
        Start-Ai | Out-Null
        Phase-Absent
        Save-All
        Stop-Ai | Out-Null
        & (Join-Path $PSScriptRoot 'install.ps1') | Out-Null
        Start-Ai | Out-Null
        Phase-Return
    }
}
Save-All
