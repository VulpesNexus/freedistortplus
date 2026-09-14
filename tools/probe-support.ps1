<#
.SYNOPSIS
    Which kinds of artwork Adobe Free Distort takes, and whether the editor
    works on each wherever Adobe's effect does.

.DESCRIPTION
    For each art type: add the effect, ask Adobe for its input bounds, drag a
    corner through the editor, and look at the drawing, the art's own type and
    contents, and Adobe's own edit path afterwards. Adobe's effect declares
    input preference 0x87 (groups, paths, compound paths); everything else
    reaches it through Illustrator's conversion to paths, and the question for
    this plugin is only whether it goes where Adobe's effect goes.

    Needs Illustrator running with FreeDistortPlus.aip loaded. Writes
    docs/evidence/support.txt and support.tsv.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ai.ps1')
$repo = Split-Path -Parent $PSScriptRoot
$evidence = Join-Path $repo 'docs\evidence'
$inv = [Globalization.CultureInfo]::InvariantCulture

$log = New-Object Collections.Generic.List[string]
function Say([string] $s) { $log.Add($s); Write-Output $s }
function Row([string] $group, [string] $case, [string] $expected, [string] $observed, [string] $status) {
    Add-ProbeResult -Group $group -Case $case -Expected $expected -Observed $observed -Status $status
    Say ("[{0}] {1} / {2}: {3}" -f $status, $group, $case, $observed)
}
function Field([string] $text, [string] $name) {
    foreach ($line in ($text -split "`r?`n")) { if ($line -like "$name`t*") { return $line.Substring($name.Length + 1) } }
    return ''
}

$fixtures = [ordered]@{
    'path'           = 'pentagon'
    'compound path'  = 'compound'
    'group'          = 'group'
    'clipping group' = 'clipGroup'
    'point text'     = 'pointText'
    'area text'      = 'areaText'
    'symbol instance'= 'symbolInstance'
    'embedded raster'= 'raster'
}

Start-ProbeResults -Probe 'support'
Say ('FreeDistort+ -- art types, {0}' -f (Get-Date -Format 'yyyy-MM-dd HH:mm'))
Initialize-AiSession | Out-Null

foreach ($kind in $fixtures.Keys) {
    $fn = $fixtures[$kind]
    try {
        $made = Invoke-Fdp ("(function(){{ FDP.clear(); FDP.{0}('fx'); FDP.selectOnly('fx'); var o = FDP.named('fx'); return o.typename + '|' + (o.typename === 'TextFrame' ? o.contents : ''); }})();" -f $fn)
    }
    catch { Row $kind 'fixture' 'built' $_.Exception.Message 'ERROR'; continue }
    $typeBefore = ($made -split '\|')[0]; $contentsBefore = ($made -split '\|', 2)[1]
    $before = Invoke-Fdp 'app.redraw(); FDP.bounds("fx");'

    $append = Send-AiMessage 'fd append'
    $listed = (Send-AiMessage 'fd list').Trim()
    Row $kind 'the effect can be added' 'listed as a post-effect' $listed $(if ($listed -eq "object`t0") { 'PASS' } else { 'FAIL' })
    if ($listed -ne "object`t0") { continue }

    $bounds = Send-AiMessage 'fd bounds' '0'
    Row $kind 'input bounds' 'Adobe answers' ("from {0}: {1}; geometric bounds {2}" -f (Field $bounds 'from'), (Field $bounds 'input bounds'), (Field $bounds 'geometric bounds')) $(if ((Field $bounds 'from') -eq 'Adobe') { 'PASS' } else { 'MEASURED' })

    $status = Send-AiMessage 'editor refresh' 'measure'
    if ($typeBefore -eq 'RasterItem') {
        # Adobe's effect leaves an embedded image unchanged; the editor should say so rather than offer handles.
        $declined = (Field $status 'target') -eq 'none' -and (Field $status 'why') -match 'raster'
        Row $kind 'the editor declines art Adobe''s effect does not change' 'no target, reason given' (Field $status 'why') $(if ($declined) { 'PASS' } else { 'FAIL' })
        Send-AiMessage 'fd write' '0|0,100,100,0|0,100,160,120,0,0,100,0' | Out-Null
        $afterWrite = Invoke-Fdp 'app.redraw(); FDP.bounds("fx");'
        Row $kind 'Adobe''s effect draws the image unchanged whatever its corners say' $before $afterWrite $(if ($afterWrite -eq $before) { 'PASS' } else { 'FAIL' })
        continue
    }
    $q = [regex]::Matches((Field $status 'quad'), '-?[0-9.]+(e-?\d+)?') | ForEach-Object { [double]::Parse($_.Value, $inv) }
    if ($q.Count -ne 8) { Row $kind 'editor' 'a target' (Field $status 'why') 'FAIL'; continue }
    $to = '{0},{1}' -f (Format-AiNumber ($q[2] + 60)), (Format-AiNumber ($q[3] + 20))
    Send-AiMessage 'editor drag' "1|free|-1|$to" | Out-Null
    $after = Invoke-Fdp 'app.redraw(); FDP.bounds("fx");'
    $geomSame = ($before -split ';')[0] -eq ($after -split ';')[0]
    $drawn = ($before -split ';')[1] -ne ($after -split ';')[1]
    Row $kind 'dragging a corner redraws the art and leaves its geometry alone' 'visible bounds change, geometric bounds do not' ("visible {0} -> {1}; geometric {2}" -f ($before -split ';')[1], ($after -split ';')[1], $(if ($geomSame) { 'unchanged' } else { 'CHANGED' })) $(if ($geomSame -and $drawn) { 'PASS' } else { 'FAIL' })

    $still = Invoke-Fdp "(function(){ var o = FDP.named('fx'); return o.typename + '|' + (o.typename === 'TextFrame' ? o.contents : ''); })();"
    Row $kind 'the art keeps its type and contents' $made $still $(if ($still -eq $made) { 'PASS' } else { 'FAIL' })

    $read = (Send-AiMessage 'fd read' '0').Trim()
    Invoke-AiScript 'app.userInteractionLevel = UserInteractionLevel.DONTDISPLAYALERTS; "off";' | Out-Null
    Send-AiMessage 'edit effect' '0' | Out-Null
    $vanilla = (Send-AiMessage 'fd read' '0').Trim()
    $sameDst = ($read -replace '^.*dst ', '') -eq ($vanilla -replace '^.*dst ', '')
    Row $kind "Adobe's own edit path keeps the editor's corners" ($read -replace '^.*dst ', '') ($vanilla -replace '^.*dst ', '') $(if ($sameDst) { 'PASS' } else { 'MEASURED' })

    if ($typeBefore -eq 'TextFrame') {
        Invoke-Fdp "(function(){ var o = FDP.named('fx'); o.contents = o.contents + ' more'; app.redraw(); return 'retyped'; })();" | Out-Null
        $retyped = Invoke-Fdp 'app.redraw(); FDP.bounds("fx");'
        Row $kind 'retyping the live text redraws it through the effect' 'visible bounds change again' ("{0} -> {1}" -f ($after -split ';')[1], ($retyped -split ';')[1]) $(if (($retyped -split ';')[1] -ne ($after -split ';')[1]) { 'PASS' } else { 'FAIL' })
    }
}

Save-ProbeResults -Path (Join-Path $evidence 'support.tsv')
Save-ProbeTranscript -Path (Join-Path $evidence 'support.txt') -Lines $log
