<#
.SYNOPSIS
    Numeric entry and arrow keys, in the host: Illustrator's own unit parser
    and ruler, the corners dialog committing into Adobe's dictionary, and a
    selected corner moved by arrow keys.

.DESCRIPTION
    The dialog is modal, so the bridge call that opens it does not return
    until it closes. A background job makes that call; this script finds the
    dialog in Illustrator's process and works it with window messages -- text
    into a field, the field committed, a button, a key posted to a field --
    which neither move the real pointer nor take the keyboard, and the dialog
    is opened without taking the foreground. Everything else is read over COM
    after the dialog has closed.

    Arrow keys are posted to the document view, where the plugin's message
    hook on Illustrator's UI thread sees them as it would a key press.

    Needs Illustrator running with FreeDistortPlus.aip loaded. Writes
    docs/evidence/numeric.txt, numeric.tsv, and numeric-dialog-preview.png.
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
function Record([string] $group, [string] $case, [string] $observed) {
    Add-ProbeResult -Group $group -Case $case -Observed $observed -Status 'MEASURED'
    Say ("[MEASURED] {0}: {1}" -f $case, $observed)
}
function Field([string] $text, [string] $name) {
    foreach ($line in ($text -split "`r?`n")) { if ($line -like "$name`t*") { return $line.Substring($name.Length + 1) } }
    return ''
}
function Dst { ((Send-AiMessage 'fd read' '0') -replace '^.*dst ', '').Trim() }
function Past { [int] (Field (Send-AiMessage 'undo count') 'past') }

Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class Win {
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr p, EnumProc cb, IntPtr l);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
    [DllImport("user32.dll")] public static extern IntPtr GetParent(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, string l);
    [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, StringBuilder l);
    // GetWindowText does not ask a control in another process for its text; WM_GETTEXT does.
    public static string ControlText(IntPtr h) { var s = new StringBuilder(128); SendMessageW(h, 0x000D, (IntPtr) 128, s); return s.ToString(); }
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    public static string Cls(IntPtr h) { var s = new StringBuilder(128); GetClassNameW(h, s, 128); return s.ToString(); }
    public static string Txt(IntPtr h) { var s = new StringBuilder(128); GetWindowTextW(h, s, 128); return s.ToString(); }
    public static IntPtr TopOfClass(uint pid, string cls) {
        IntPtr hit = IntPtr.Zero;
        EnumWindows(delegate(IntPtr h, IntPtr l) {
            uint p; GetWindowThreadProcessId(h, out p);
            if (p == pid && IsWindowVisible(h) && Cls(h) == cls) { hit = h; return false; }
            return true;
        }, IntPtr.Zero);
        return hit;
    }
    public static IntPtr DocumentView(IntPtr main) {
        IntPtr hit = IntPtr.Zero;
        EnumChildWindows(main, delegate(IntPtr h, IntPtr x) {
            if (Txt(h) != "OS_ViewContainer") return true;
            if (Cls(GetParent(h)) == "OWL.Document") { hit = h; return false; }
            return true;
        }, IntPtr.Zero);
        return hit;
    }
}
"@

$WM_SETTEXT = 0x000C; $WM_COMMAND = 0x0111; $WM_KEYDOWN = 0x0100; $WM_KEYUP = 0x0101
$EN_KILLFOCUS = 0x0200; $IDOK = 1; $IDCANCEL = 2; $kEdit = 2000; $kOffset = 2102
$illustrator = [uint32] (Get-Process Illustrator | Select-Object -First 1).Id

function Dialog([int] $timeoutMs = 15000) {
    $deadline = (Get-Date).AddMilliseconds($timeoutMs)
    while ((Get-Date) -lt $deadline) {
        $h = [Win]::TopOfClass($illustrator, 'VulpesNexusFreeDistortCorners')
        if ($h -ne [IntPtr]::Zero) { return $h }
        Start-Sleep -Milliseconds 100
    }
    return [IntPtr]::Zero
}
function MakeWParam([int] $low, [int] $high) { [IntPtr] (($high -shl 16) -bor ($low -band 0xFFFF)) }
function SetField($dlg, [int] $k, [string] $text) {
    $edit = [Win]::GetDlgItem($dlg, $kEdit + $k)
    [Win]::SendMessageW($edit, $WM_SETTEXT, [IntPtr]::Zero, $text) | Out-Null
    [Win]::SendMessageW($dlg, $WM_COMMAND, (MakeWParam ($kEdit + $k) $EN_KILLFOCUS), $edit) | Out-Null
}
function FieldText($dlg, [int] $k) { [Win]::ControlText([Win]::GetDlgItem($dlg, $kEdit + $k)) }
function Press($dlg, [int] $id) { [Win]::PostMessageW($dlg, $WM_COMMAND, (MakeWParam $id 0), [IntPtr]::Zero) | Out-Null }

# One dialog session: the job opens it; $work runs against the window.
function Session([int] $corner, [scriptblock] $work) {
    $job = Start-Job -ScriptBlock {
        param($tools, $corner)
        . (Join-Path $tools 'ai.ps1')
        Send-AiMessage 'editor numeric' ("{0}|0" -f $corner)
    } -ArgumentList $PSScriptRoot, $corner
    $dlg = Dialog
    if ($dlg -eq [IntPtr]::Zero) { Stop-Job $job; Remove-Job $job -Force; throw 'the corners dialog never appeared' }
    Start-Sleep -Milliseconds 400
    $foreground = [Win]::GetForegroundWindow() -eq $dlg
    $observed = & $work $dlg
    $said = (Receive-Job -Job $job -Wait -AutoRemoveJob | Out-String).Trim()
    return [pscustomobject]@{ Said = $said; Observed = $observed; TookForeground = $foreground }
}

Start-ProbeResults -Probe 'numeric'
Say ('FreeDistort+ -- numeric entry and arrow keys, {0}' -f (Get-Date -Format 'yyyy-MM-dd HH:mm'))
Initialize-AiSession | Out-Null

# ---- Illustrator's parser and ruler ---------------------------------------------

foreach ($pair in @(@('12.5', 12.5), @('12,5', 12.5), @('1 in', 72), @('3 mm', 8.50393700787), @('10+5', 15), @('-15.5 pt', -15.5), @('12.3456789', 12.3456789))) {
    $r = Send-AiMessage 'units parse' $pair[0]
    $points = Field $r 'points'
    $ok = (Field $r 'accepted') -eq 'yes' -and [math]::Abs([double]::Parse($points, $inv) - $pair[1]) -lt 1e-9
    Check 'units' ("a field typed '{0}' is taken as {1} pt" -f $pair[0], $pair[1]) ("{0} pt" -f $pair[1]) ("accepted {0}, {1} pt, shown as {2}" -f (Field $r 'accepted'), $points, (Field $r 'evaluated')) $ok
}
$r = Send-AiMessage 'units parse' 'abc'
Check 'units' "a field typed 'abc' is not a number" 'not accepted' ("accepted {0}" -f (Field $r 'accepted')) ((Field $r 'accepted') -eq 'no')
Record 'units' 'how Illustrator formats 12.3456789 pt for a field' (Field (Send-AiMessage 'units format' '12.3456789') 'formatted')

Invoke-Fdp "FDP.clear(); FDP.pentagon('pent'); FDP.selectOnly('pent');" | Out-Null
$ruler = Send-AiMessage 'coords' '90,330'
$scripted = Invoke-AiScript '(function(){ var o = app.activeDocument.pageItems.getByName("pent"); app.coordinateSystem = CoordinateSystem.ARTBOARDCOORDINATESYSTEM; var g = o.geometricBounds; app.coordinateSystem = CoordinateSystem.DOCUMENTCOORDINATESYSTEM; return g[0] + "," + (-g[1]); })();'
Check 'ruler' "the dialog's positions are the ruler's, as Illustrator's artboard coordinates give them" $scripted (Field $ruler 'ruler') ((Field $ruler 'ruler') -eq $scripted)
Check 'ruler' 'the ruler conversion round-trips exactly' '90,330' (Field $ruler 'back') ((Field $ruler 'back') -eq '90,330')
Record 'ruler' "AIHardSoftSuite::ConvertCoordinates back from the ruler, with convertForDisplay, for contrast" (Field $ruler 'host reverse')

# ---- the dialog -------------------------------------------------------------------

function Fresh {
    Invoke-Fdp "FDP.clear(); FDP.pentagon('pent'); FDP.selectOnly('pent');" | Out-Null
    Send-AiMessage 'fd append' | Out-Null
    Send-AiMessage 'editor open' | Out-Null
    Send-AiMessage 'editor drag' '1|free|-1|400.123456789,330' | Out-Null
    Send-AiMessage 'undo clear' | Out-Null
}

Fresh
$before = Dst; $pastBefore = Past
$s = Session 1 { param($dlg) $shown = FieldText $dlg 2; Press $dlg $IDOK; $shown }
Check 'dialog' 'OK with nothing typed changes nothing, though a field shows fewer digits than the corner has' $before ("{0}; field showed {1}; {2}" -f (Dst), $s.Observed, $s.Said) ((Dst) -eq $before -and $s.Said -match 'nothing changed')
Check 'dialog' '...and adds no undo step' ("past {0}" -f $pastBefore) ("past {0}" -f (Past)) ((Past) -eq $pastBefore)
Check 'dialog' 'a dialog opened for a test does not take the foreground' 'not the foreground window' $(if ($s.TookForeground) { 'foreground' } else { 'not foreground' }) (-not $s.TookForeground)

Fresh
$s = Session 3 { param($dlg) Press $dlg $kOffset; Start-Sleep -Milliseconds 200; SetField $dlg 6 '25 pt'; Start-Sleep -Milliseconds 300; Press $dlg $IDOK }
Check 'dialog' 'an offset of 25 pt on the bottom-right X moves that corner 25 pt right of the undistorted corner' '(90,330 400.123456789,330 90,100 365,100)' ("{0}; {1}" -f (Dst), $s.Said) ((Dst) -eq '(90,330 400.123456789,330 90,100 365,100)')
Check 'dialog' '...as exactly one undo step' 'past 1' ("past {0}" -f (Past)) ((Past) -eq 1)
Invoke-AiScript 'app.undo(); app.redraw(); "undone";' | Out-Null
Check 'dialog' '...which one Undo takes back' '(90,330 400.123456789,330 90,100 340,100)' (Dst) ((Dst) -eq '(90,330 400.123456789,330 90,100 340,100)')

Fresh
$s = Session 0 { param($dlg) Press $dlg 2101; Start-Sleep -Milliseconds 200; SetField $dlg 1 '200 pt'; Start-Sleep -Milliseconds 800
    $shot = & (Join-Path $PSScriptRoot 'capture-view.ps1') -Path (Join-Path $evidence 'numeric-dialog-preview.png'); Press $dlg $IDOK; $shot }
Say ("preview capture while open: {0}" -f $s.Observed)
Check 'dialog' 'a position of 200 pt on the top-left Y puts that corner 200 pt below the artboard top, as the ruler measures' '(90,400 400.123456789,330 90,100 340,100)' ("{0}; {1}" -f (Dst), $s.Said) ((Dst) -eq '(90,400 400.123456789,330 90,100 340,100)')

Fresh
$before = Dst; $pastBefore = Past
$s = Session 2 { param($dlg) SetField $dlg 4 '12,5'; Start-Sleep -Milliseconds 300; SetField $dlg 5 '1 in'; Start-Sleep -Milliseconds 300; Press $dlg $IDCANCEL }
Check 'dialog' 'Cancel after typing and previewing puts the dictionary back exactly' $before ("{0}; {1}" -f (Dst), $s.Said) ((Dst) -eq $before -and $s.Said -match 'canceled')
Check 'dialog' '...and leaves no undo step' ("past {0}" -f $pastBefore) ("past {0}" -f (Past)) ((Past) -eq $pastBefore)

Fresh
$s = Session 1 { param($dlg) $edit = [Win]::GetDlgItem($dlg, $kEdit + 2)
    [Win]::PostMessageW($edit, $WM_KEYDOWN, [IntPtr] 0x26, [IntPtr]::Zero) | Out-Null; Start-Sleep -Milliseconds 300
    [Win]::PostMessageW($edit, $WM_KEYDOWN, [IntPtr] 0x26, [IntPtr]::Zero) | Out-Null; Start-Sleep -Milliseconds 300
    $shown = FieldText $dlg 2; Press $dlg $IDOK; $shown }
Check 'dialog' 'the up arrow in a field steps it by one ruler unit, twice' '(90,330 402.123456789,330 90,100 340,100)' ("{0}; field showed {1}" -f (Dst), $s.Observed) ((Dst) -eq '(90,330 402.123456789,330 90,100 340,100)')

Fresh
$s = Session 2 { param($dlg) $edit = [Win]::GetDlgItem($dlg, $kEdit + 4)
    [Win]::SendMessageW($edit, $WM_SETTEXT, [IntPtr]::Zero, '1 in') | Out-Null
    [Win]::PostMessageW($edit, $WM_KEYDOWN, [IntPtr] 0x0D, [IntPtr]::Zero) | Out-Null; Start-Sleep -Milliseconds 500 }
Check 'dialog' 'Enter in a field typed 1 in takes it and closes: the bottom-left X is 72 pt on the ruler' '(90,330 400.123456789,330 72,100 340,100)' ("{0}; {1}" -f (Dst), $s.Said) ((Dst) -eq '(90,330 400.123456789,330 72,100 340,100)')

# ---- arrow keys ----------------------------------------------------------------------

$view = [Win]::DocumentView((Get-Process Illustrator | Select-Object -First 1).MainWindowHandle)
function Arrow([int] $vk) { [Win]::PostMessageW($view, $WM_KEYDOWN, [IntPtr] $vk, [IntPtr]::Zero) | Out-Null; [Win]::PostMessageW($view, $WM_KEYUP, [IntPtr] $vk, [IntPtr]::Zero) | Out-Null; Start-Sleep -Milliseconds 400; Invoke-AiScript 'app.redraw(); "r";' | Out-Null }

Fresh
$increment = Field (Send-AiMessage 'editor status') 'keyboard increment'
Send-AiMessage 'editor corner' '3' | Out-Null
Arrow 0x27   # right
Arrow 0x26   # up
$status = Send-AiMessage 'editor status'
$step = [double]::Parse($increment, $inv)
$want = '(90,330 400.123456789,330 90,100 {0},{1})' -f (Format-AiNumber (340 + $step)), (Format-AiNumber (100 + $step))
Check 'keys' 'with a corner selected, Right then Up move that corner by the keyboard increment each' $want ("{0}; increment {1}; {2}" -f (Dst), $increment, (Field $status 'nudges')) ((Dst) -eq $want)
Check 'keys' '...one undo step per key' 'past 2' ("past {0}" -f (Past)) ((Past) -eq 2)
$geometry = Invoke-Fdp 'FDP.bounds("pent");'
Check 'keys' '...and the art itself is not nudged' '90,330,340,100;...' $geometry ($geometry -like '90,330,340,100;*')

Fresh
Send-AiMessage 'editor corner' '-1' | Out-Null
$dstBefore = Dst
Arrow 0x27
$geometry = Invoke-Fdp 'FDP.bounds("pent");'
Record 'keys' 'with no corner selected, Right is left to Illustrator: the art geometry, and the dictionary' ("{0}; {1}" -f $geometry, (Dst))
Check 'keys' 'with no corner selected, the plugin does not touch the dictionary' $dstBefore (Dst) ((Dst) -eq $dstBefore)

Save-ProbeResults -Path (Join-Path $evidence 'numeric.tsv')
Save-ProbeTranscript -Path (Join-Path $evidence 'numeric.txt') -Lines $log
