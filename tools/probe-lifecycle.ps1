<#
.SYNOPSIS
    What happens to the editor when the document changes under it: the object
    deleted, the effect removed or moved, undo and redo between drags, the
    document closed and another opened -- and a repeated-use stress run that
    watches Illustrator's handle, GDI, and memory counts.

.DESCRIPTION
    Every case asks one thing: does the editor ever write into something that
    is no longer what it was editing, and does it leak. Drags run through the
    bridge's "editor drag", which is the mouse's own drag code.

    Needs Illustrator running with FreeDistortPlus.aip loaded. Writes
    docs/evidence/lifecycle.txt and lifecycle.tsv. -Cycles sets the length of
    the stress run.
#>
[CmdletBinding()]
param([int] $Cycles = 60)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ai.ps1')
$repo = Split-Path -Parent $PSScriptRoot
$evidence = Join-Path $repo 'docs\evidence'

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
function Dst([int] $index = 0) { ((Send-AiMessage 'fd read' ([string] $index)) -replace '^.*dst ', '').Trim() }
function Fresh([string] $name = 'pent') {
    Invoke-Fdp ("FDP.clear(); FDP.pentagon('{0}'); FDP.selectOnly('{0}');" -f $name) | Out-Null
    Send-AiMessage 'fd append' | Out-Null
    Send-AiMessage 'editor open' | Out-Null
}

Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Res {
    [DllImport("user32.dll")] public static extern uint GetGuiResources(IntPtr process, uint flags);
}
"@
function Counters {
    $p = Get-Process Illustrator | Select-Object -First 1
    $p.Refresh()
    [pscustomobject]@{ Handles = $p.HandleCount; Gdi = [Res]::GetGuiResources($p.Handle, 0); User = [Res]::GetGuiResources($p.Handle, 1); PrivateMB = [math]::Round($p.PrivateMemorySize64 / 1MB) }
}

Start-ProbeResults -Probe 'lifecycle'
Say ('FreeDistort+ -- lifecycle and repeated use, {0}' -f (Get-Date -Format 'yyyy-MM-dd HH:mm'))
Initialize-AiSession | Out-Null

# ---- the object deleted under the editor -------------------------------------------

Fresh
Send-AiMessage 'editor drag' '1|free|-1|400,340' | Out-Null
Invoke-Fdp 'FDP.named("pent").remove(); app.redraw(); "removed";' | Out-Null
$status = Send-AiMessage 'editor status'
$drag = Send-AiMessage 'editor drag' '1|free|-1|420,360'
Check 'deleted' 'after its object is deleted the editor holds no target and a drag writes nothing' 'target none; could not begin' ("target {0}; {1}" -f (Field $status 'target'), $drag.Trim()) ((Field $status 'target') -eq 'none' -and $drag -match 'could not begin')

# ---- the effect removed, and another effect put ahead of it --------------------------

Fresh
Send-AiMessage 'editor drag' '1|free|-1|400,340' | Out-Null
Send-AiMessage 'remove effect' '0' | Out-Null
$drag = Send-AiMessage 'editor drag' '1|free|-1|420,360'
$status = Send-AiMessage 'editor status'
Check 'removed' 'after its Free Distort is removed from the appearance a drag writes nothing and says why' 'could not begin: no Free Distort' ("{0}; why {1}" -f $drag.Trim(), (Field $status 'why')) ($drag -match 'could not begin' -and (Field $status 'why') -match 'no Free Distort')

Fresh
Send-AiMessage 'editor drag' '1|free|-1|400,340' | Out-Null
Send-AiMessage 'apply effect' 'Adobe Transform|scaleH_Factor=r:1;scaleV_Factor=r:1;moveH_Pts=r:0;moveV_Pts=r:0' | Out-Null
Send-AiMessage 'move effect' '1,0' | Out-Null
$appearance = Send-AiMessage 'appearance'
$drag = Send-AiMessage 'editor drag' '3|free|-1|360,80'
$status = Send-AiMessage 'editor status'
$transform = ([regex]::Match((Send-AiMessage 'appearance'), '\[0\] "([^"]+)"')).Groups[1].Value
Check 'moved' 'with another effect moved ahead of it, a drag edits the Free Distort at its new index and leaves the other effect alone' 'post-effect 1; effect 0 still Adobe Transform; corner 3 at 360,80' ("post-effect {0}; effect 0 {1}; {2}" -f (Field $status 'post-effect'), $transform, (Dst 1)) ((Field $status 'post-effect') -eq '1' -and $transform -eq 'Adobe Transform' -and (Dst 1) -match '360,80\)$')

# ---- undo and redo between drags ---------------------------------------------------------

Fresh
Send-AiMessage 'editor drag' '1|free|-1|400,340' | Out-Null
Send-AiMessage 'editor drag' '2|free|-1|70,80' | Out-Null
Invoke-AiScript 'app.undo(); app.redraw(); "u";' | Out-Null
Send-AiMessage 'editor drag' '3|free|-1|360,90' | Out-Null
Check 'undo' 'a drag after an Undo starts from the state the Undo left, not from the undone one' '(90,330 400,340 90,100 360,90)' (Dst) ((Dst) -eq '(90,330 400,340 90,100 360,90)')
Invoke-AiScript 'app.undo(); app.undo(); app.redo(); app.redraw(); "u";' | Out-Null
Send-AiMessage 'editor refresh' | Out-Null
Send-AiMessage 'editor drag' '0|free|-1|80,340' | Out-Null
Check 'undo' '...and after Undo, Undo, Redo' '(80,340 400,340 90,100 340,100)' (Dst) ((Dst) -eq '(80,340 400,340 90,100 340,100)')

# ---- the document closed under the editor ---------------------------------------------------

Fresh
Send-AiMessage 'editor drag' '1|free|-1|400,340' | Out-Null
Invoke-AiScript '(function(){ for (var i = 0; i < app.documents.length; i++) { if (app.documents[i].name === "fdp-probe.ai") { app.documents[i].close(SaveOptions.DONOTSAVECHANGES); return "closed"; } } return "not found"; })();' | Out-Null
$status = Send-AiMessage 'editor status'
Check 'close' "closing the editor's document leaves it with no target, and Illustrator running" 'target none' ("target {0}" -f (Field $status 'target')) ((Field $status 'target') -eq 'none')
Initialize-AiSession | Out-Null
Fresh
Send-AiMessage 'editor drag' '1|free|-1|410,350' | Out-Null
Check 'close' '...and in the document opened next, the editor edits normally' '(90,330 410,350 90,100 340,100)' (Dst) ((Dst) -eq '(90,330 410,350 90,100 340,100)')

# ---- repeated use -------------------------------------------------------------------------------

Fresh
Send-AiMessage 'editor drag' '1|free|-1|400,340' | Out-Null
$warm = Counters
Say ("before {0} cycles: handles {1}, GDI {2}, USER {3}, private {4} MB" -f $Cycles, $warm.Handles, $warm.Gdi, $warm.User, $warm.PrivateMB)
$failures = 0
for ($i = 1; $i -le $Cycles; $i++) {
    $corner = $i % 4
    Send-AiMessage 'editor open' | Out-Null
    Send-AiMessage 'editor drag' ("{0}|free|2|300,200;310,210;320,220;330,230" -f $corner) | Out-Null          # canceled part-way
    Send-AiMessage 'editor drag' ("{0}|symmetric|-1|{1},{2}" -f $corner, (200 + $i), (200 - $i)) | Out-Null   # committed
    Send-AiMessage 'editor preview open' ("{0}|converging|{1},{2}" -f ((3 - $corner)), (250 + $i), 150) | Out-Null
    Send-AiMessage 'editor preview close' | Out-Null
    Invoke-AiScript 'app.undo(); app.undo(); app.redo(); app.redraw(); "u";' | Out-Null
    Send-AiMessage 'editor corner' ([string] $corner) | Out-Null
    Send-AiMessage 'editor refresh' 'measure' | Out-Null
    Send-AiMessage 'tool select' 'Adobe Select Tool' | Out-Null
    if (-not (Get-Process Illustrator -ErrorAction SilentlyContinue)) { $failures++; break }
    if ($i % 20 -eq 0) { $c = Counters; Say ("after {0}: handles {1}, GDI {2}, USER {3}, private {4} MB" -f $i, $c.Handles, $c.Gdi, $c.User, $c.PrivateMB) }
}
$done = Counters
Check 'stress' ("{0} cycles of open, canceled drag, committed drag, preview, undo and redo, select, measure, and tool switch complete with Illustrator alive" -f $Cycles) 'alive' $(if ($failures -eq 0) { 'alive' } else { 'Illustrator exited' }) ($failures -eq 0)
Check 'stress' '...without GDI or USER objects growing' ("GDI {0}, USER {1}, within 20" -f $warm.Gdi, $warm.User) ("GDI {0}, USER {1}" -f $done.Gdi, $done.User) ([math]::Abs([int] $done.Gdi - [int] $warm.Gdi) -le 20 -and [math]::Abs([int] $done.User - [int] $warm.User) -le 20)
Record 'stress' 'handles and private memory before and after' ("handles {0} -> {1}; private {2} MB -> {3} MB" -f $warm.Handles, $done.Handles, $warm.PrivateMB, $done.PrivateMB)

Save-ProbeResults -Path (Join-Path $evidence 'lifecycle.tsv')
Save-ProbeTranscript -Path (Join-Path $evidence 'lifecycle.txt') -Lines $log
