<#
.SYNOPSIS
    The live drag preview: that it is drawn on the canvas, and that every point
    it shows is where Adobe's effect puts that point once the drag is released.

.DESCRIPTION
    Illustrator does not repaint the document inside a tool's drag loop, so
    during a drag the editor draws Adobe's result itself (DistortEditor.h,
    PreviewContour). This opens a drag through the bridge and leaves it open,
    captures the canvas with PrintWindow -- no foreground, no input -- reads
    back every previewed anchor and handle, commits, and compares them with
    what Adobe drew.

    Needs Illustrator running with EnhancedFreeDistort.aip loaded. Writes
    docs/evidence/preview.txt, preview.tsv, and editor-preview-open.png.
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
function Check([string] $group, [string] $case, [string] $expected, [string] $observed, [bool] $ok) {
    $status = if ($ok) { 'PASS' } else { 'FAIL' }
    Add-ProbeResult -Group $group -Case $case -Expected $expected -Observed $observed -Status $status
    Say ("[{0}] {1}: {2}" -f $status, $case, $observed)
}
function Field([string] $text, [string] $name) {
    foreach ($line in ($text -split "`r?`n")) { if ($line -like "$name`t*") { return $line.Substring($name.Length + 1) } }
    return ''
}
function Points([string] $text) {
    foreach ($line in ($text -split "`r?`n")) {
        if ($line -match '^(-?[0-9.e+-]+),(-?[0-9.e+-]+)$') { ,@([double]::Parse($Matches[1], $inv), [double]::Parse($Matches[2], $inv)) }
    }
}
function Rendered([string] $name) {
    $rows = (Invoke-Efd ("EFD.sourceAndResult('{0}');" -f $name)) -split "`r?`n" | Where-Object { $_ -like 'R*' }
    foreach ($r in $rows) {
        $f = $r -split "`t"
        # Adobe's rows are anchor, left (in) handle, right (out) handle; the
        # preview lists in, anchor, out.
        ,@([double]::Parse($f[4], $inv), [double]::Parse($f[5], $inv))
        ,@([double]::Parse($f[2], $inv), [double]::Parse($f[3], $inv))
        ,@([double]::Parse($f[6], $inv), [double]::Parse($f[7], $inv))
    }
}
function Compare-Ordered($a, $b) {
    if ($a.Count -ne $b.Count) { return [double]::PositiveInfinity }
    $worst = 0.0
    for ($i = 0; $i -lt $a.Count; $i++) { $worst = [math]::Max($worst, [math]::Sqrt([math]::Pow($a[$i][0] - $b[$i][0], 2) + [math]::Pow($a[$i][1] - $b[$i][1], 2))) }
    $worst
}
function Compare-Sets($a, $b) {
    # Each previewed point against its nearest rendered point: for artwork
    # whose expansion does not keep the preview's path order.
    $worst = 0.0
    foreach ($p in $a) {
        $best = [double]::PositiveInfinity
        foreach ($q in $b) { $d = [math]::Pow($p[0] - $q[0], 2) + [math]::Pow($p[1] - $q[1], 2); if ($d -lt $best) { $best = $d } }
        $worst = [math]::Max($worst, [math]::Sqrt($best))
    }
    $worst
}

Start-ProbeResults -Probe 'preview'
Say ('Enhanced Free Distort -- the live drag preview, {0}' -f (Get-Date -Format 'yyyy-MM-dd HH:mm'))
Initialize-AiSession | Out-Null

# ---- a 30-anchor path, starting from a distorted quad ----------------------------------

Invoke-Efd "EFD.clear(); EFD.grid('fx');" | Out-Null
Invoke-Efd "EFD.selectOnly('fx'); app.redraw();" | Out-Null
Send-AiMessage 'fd append' | Out-Null
Send-AiMessage 'editor open' | Out-Null
Send-AiMessage 'editor drag' '1|free|-1|450,290' | Out-Null
Invoke-AiScript '(function(){ var v = app.activeDocument.views[0]; v.zoom = 0.45; v.centerPoint = [260, 200]; app.redraw(); return "view"; })();' | Out-Null
Send-AiMessage 'editor refresh' 'measure' | Out-Null
$opened = Send-AiMessage 'editor preview open' '2|free|60,70'
Say ($opened.TrimEnd())
$status = Send-AiMessage 'editor status'
Check 'grid' 'a drag opens with a preview of the art' 'drag open; preview of 1 path, 30 segments, captured before the drag because a tool mouse-down cannot read the styled result' ("{0}; {1}" -f (Field $status 'drag'), (Field $status 'last preview')) ((Field $status 'drag') -like 'open*' -and (Field $status 'last preview') -like 'preview of 1 paths, 30 segments*')
Invoke-AiScript 'app.redraw(); "drawn";' | Out-Null
Start-Sleep -Milliseconds 700
$shot = & (Join-Path $PSScriptRoot 'capture-view.ps1') -Path (Join-Path $evidence 'editor-preview-open.png')
Say "capture: $shot"
$preview = @(Points (Send-AiMessage 'editor preview points'))
Say (Send-AiMessage 'editor preview close').TrimEnd()
$rendered = @(Rendered 'fx')
$worst = Compare-Ordered $preview $rendered
Check 'grid' 'every previewed anchor and handle is where Adobe draws it after release' 'within 1e-6 pt' ("{0} points previewed, {1} rendered, worst deviation {2} pt" -f $preview.Count, $rendered.Count, $worst.ToString('G3', $inv)) ($preview.Count -eq 90 -and $worst -lt 1e-6)

# ---- live point text -------------------------------------------------------------------

Invoke-Efd "EFD.clear(); EFD.pointText('fx');" | Out-Null
Invoke-Efd "EFD.selectOnly('fx'); app.redraw();" | Out-Null
Send-AiMessage 'fd append' | Out-Null
Send-AiMessage 'editor refresh' 'measure' | Out-Null
$status = Send-AiMessage 'editor status'
$q = [regex]::Matches((Field $status 'quad'), '-?[0-9.]+') | ForEach-Object { [double]::Parse($_.Value, $inv) }
$opened = Send-AiMessage 'editor preview open' ("3|free|{0},{1}" -f (Format-AiNumber ($q[6] + 40)), (Format-AiNumber ($q[7] - 30)))
Say ($opened.TrimEnd())
$preview = @(Points (Send-AiMessage 'editor preview points'))
Say (Send-AiMessage 'editor preview close').TrimEnd()
$rendered = @(Rendered 'fx')
$worst = Compare-Sets $preview $rendered
Check 'point text' 'the preview of live text matches the glyph outlines Adobe draws after release' 'within 1e-6 pt' ("{0} points previewed, {1} rendered, worst nearest distance {2} pt" -f $preview.Count, $rendered.Count, $worst.ToString('G3', $inv)) ($preview.Count -gt 0 -and $worst -lt 1e-6)
$still = Invoke-Efd "(function(){ var o = EFD.named('fx'); return o.typename + '|' + o.contents; })();"
Check 'point text' 'the text is still live text afterwards' 'TextFrame|Distort' $still ($still -eq 'TextFrame|Distort')

Save-ProbeResults -Path (Join-Path $evidence 'preview.tsv')
Save-ProbeTranscript -Path (Join-Path $evidence 'preview.txt') -Lines $log
