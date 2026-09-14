<#
.SYNOPSIS
    Records what Illustrator's own Free Transform tool does to a path, one
    drag made by a person at a time, read back over COM.

.DESCRIPTION
    Illustrator's tools receive only real input, so a person drags; this
    script never touches the mouse, the keyboard, or the foreground.

      -Prepare            puts a fresh grid path (a closed serpentine through
                          6 x 5 anchors, bounds 100..400 x 100..300) in the
                          probe document, selects it, selects Illustrator's
                          Free Transform tool, and centers the view on it.
      -Operation <name>   after the person's drag: records every anchor and
                          handle of the path with -Description saying what was
                          done, then prepares the fixture again for the next.

    tools/solve-free-transform.py classifies each recorded operation: which
    corners moved, and whether the result is affine, bilinear from the four
    corners (so Adobe Free Distort could store it), or projective.
    Writes docs/evidence/free-transform-points.tsv.
#>
[CmdletBinding()]
param(
    [switch] $Prepare,
    [string] $Operation = '',
    [string] $Description = ''
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ai.ps1')
$repo = Split-Path -Parent $PSScriptRoot
$points = Join-Path $repo 'docs\evidence\free-transform-points.tsv'

function Fixture {
    Invoke-AiScript 'app.userInteractionLevel = UserInteractionLevel.DONTDISPLAYALERTS; "off";' | Out-Null
    Invoke-AiScript -Path (Join-Path $PSScriptRoot 'fixtures.jsx') | Out-Null
    Invoke-Fdp 'FDP.doc(); "ready";' | Out-Null
    Invoke-Fdp "FDP.clear(); FDP.grid('ft'); FDP.selectOnly('ft');" | Out-Null
    Invoke-AiScript '(function(){ var v = app.activeDocument.views[0]; v.zoom = 1; v.centerPoint = [250, 200]; app.redraw(); return "view"; })();' | Out-Null
    $selected = Send-AiMessage 'tool select' 'Adobe Free Transform Tool'
    $tool = (Send-AiMessage 'tools') -split "`r?`n" | Where-Object { $_ -like "*`t(selected)" }
    Write-Output ("fixture ready; tool: {0}" -f ($tool -replace "`t\(selected\)", ''))
}

if ($Operation) {
    $rows = (Invoke-Fdp "(function(){ var p = FDP.named('ft'); var out = []; for (var i = 0; i < p.pathPoints.length; i++) { var q = p.pathPoints[i]; out.push([i, FDP.f(q.anchor[0]), FDP.f(q.anchor[1]), FDP.f(q.leftDirection[0]), FDP.f(q.leftDirection[1]), FDP.f(q.rightDirection[0]), FDP.f(q.rightDirection[1])].join(String.fromCharCode(9))); } return out.join(String.fromCharCode(10)); })();") -split "`r?`n" | Where-Object { $_ }
    if (-not (Test-Path $points)) {
        [IO.File]::WriteAllLines($points, @("operation`tdescription`tindex`tah`tav`tlh`tlv`trh`trv"))
    }
    $clean = ($Description -replace "[`t`r`n]+", ' ').Trim()
    [IO.File]::AppendAllLines($points, [string[]] ($rows | ForEach-Object { "{0}`t{1}`t{2}" -f $Operation, $clean, $_ }))
    Write-Output ("recorded {0}: {1} anchors" -f $Operation, $rows.Count)
    Fixture
}
elseif ($Prepare) {
    Fixture
}
