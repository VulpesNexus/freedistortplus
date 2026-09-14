<#
.SYNOPSIS
    Background driver for Adobe's Free Distort dialog; see
    Invoke-FreeDistortDialog in ai.ps1, which starts it.

.DESCRIPTION
    Waits for the "Free Distort" window of Illustrator's process, optionally
    captures it, optionally drags inside its Drover view with posted mouse
    messages (the real pointer does not move), then closes it with Enter or
    Escape, falling back to WM_CLOSE.
#>
param(
    [int] $TimeoutSeconds = 30,
    [ValidateSet('ok', 'cancel')] [string] $Button = 'ok',
    [string] $ShotPrefix = '',
    [int[]] $Drag = @(),          # x1,y1,x2,y2 in the view's client pixels
    [string] $OutFile = ''
)

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class Dd {
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr p, EnumProc cb, IntPtr l);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    public static string Txt(IntPtr h) { var s = new StringBuilder(512); GetWindowTextW(h, s, 512); return s.ToString(); }
    public static string Cls(IntPtr h) { var s = new StringBuilder(256); GetClassNameW(h, s, 256); return s.ToString(); }
    public static IntPtr FindTitled(uint pid, string title) {
        IntPtr hit = IntPtr.Zero;
        EnumWindows(delegate(IntPtr h, IntPtr l) {
            uint p; GetWindowThreadProcessId(h, out p);
            if (p == pid && IsWindowVisible(h) && Txt(h) == title) { hit = h; return false; }
            return true;
        }, IntPtr.Zero);
        return hit;
    }
    public static IntPtr FirstChild(IntPtr parent) {
        IntPtr hit = IntPtr.Zero;
        EnumChildWindows(parent, delegate(IntPtr h, IntPtr l) { hit = h; return false; }, IntPtr.Zero);
        return hit;
    }
}
"@

function Shot([IntPtr] $h, [string] $path) {
    $r = New-Object Dd+RECT
    [Dd]::GetWindowRect($h, [ref] $r) | Out-Null
    $w = $r.R - $r.L; $hh = $r.B - $r.T
    $bmp = New-Object Drawing.Bitmap $w, $hh
    $g = [Drawing.Graphics]::FromImage($bmp)
    $hdc = $g.GetHdc()
    [Dd]::PrintWindow($h, $hdc, 2) | Out-Null
    $g.ReleaseHdc($hdc); $g.Dispose()
    $bmp.Save($path, [Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
}

$log = New-Object Collections.Generic.List[string]
$pid0 = [uint32] (Get-Process Illustrator | Select-Object -First 1).Id
$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
$dlg = [IntPtr]::Zero
while ((Get-Date) -lt $deadline -and $dlg -eq [IntPtr]::Zero) {
    $dlg = [Dd]::FindTitled($pid0, 'Free Distort')
    if ($dlg -eq [IntPtr]::Zero) { Start-Sleep -Milliseconds 200 }
}
if ($dlg -eq [IntPtr]::Zero) { $log.Add('dialog never appeared'); if ($OutFile) { [IO.File]::WriteAllLines($OutFile, $log) }; return $log }
Start-Sleep -Milliseconds 1200
$view = [Dd]::FirstChild($dlg)
$vr = New-Object Dd+RECT; [Dd]::GetWindowRect($view, [ref] $vr) | Out-Null
$dr = New-Object Dd+RECT; [Dd]::GetWindowRect($dlg, [ref] $dr) | Out-Null
$log.Add(("dialog {0},{1},{2},{3} view {4},{5},{6},{7} class={8}" -f $dr.L, $dr.T, $dr.R, $dr.B, $vr.L, $vr.T, $vr.R, $vr.B, [Dd]::Cls($view)))
if ($ShotPrefix) { Shot $dlg "$ShotPrefix-open.png" }

if ($Drag.Count -eq 4) {
    $mk = { param($x, $y) [IntPtr] (($y -shl 16) -bor ($x -band 0xFFFF)) }
    [Dd]::PostMessage($view, 0x0200, [IntPtr] 0, (& $mk $Drag[0] $Drag[1])) | Out-Null
    Start-Sleep -Milliseconds 100
    [Dd]::PostMessage($view, 0x0201, [IntPtr] 1, (& $mk $Drag[0] $Drag[1])) | Out-Null
    Start-Sleep -Milliseconds 150
    for ($i = 1; $i -le 10; $i++) {
        $x = [int] ($Drag[0] + ($Drag[2] - $Drag[0]) * $i / 10)
        $y = [int] ($Drag[1] + ($Drag[3] - $Drag[1]) * $i / 10)
        [Dd]::PostMessage($view, 0x0200, [IntPtr] 1, (& $mk $x $y)) | Out-Null
        Start-Sleep -Milliseconds 60
    }
    [Dd]::PostMessage($view, 0x0202, [IntPtr] 0, (& $mk $Drag[2] $Drag[3])) | Out-Null
    Start-Sleep -Milliseconds 1000
    $log.Add("dragged $($Drag -join ',')")
    if ($ShotPrefix) { Shot $dlg "$ShotPrefix-dragged.png" }
}

$vk = if ($Button -eq 'cancel') { 0x1B } else { 0x0D }
[Dd]::PostMessage($dlg, 0x0100, [IntPtr] $vk, [IntPtr] 0) | Out-Null
[Dd]::PostMessage($dlg, 0x0101, [IntPtr] $vk, [IntPtr] 0) | Out-Null
Start-Sleep -Milliseconds 1500
if ([Dd]::IsWindow($dlg) -and [Dd]::IsWindowVisible($dlg)) {
    $log.Add('key did not close it; posting WM_CLOSE')
    [Dd]::PostMessage($dlg, 0x0010, [IntPtr] 0, [IntPtr] 0) | Out-Null
    Start-Sleep -Milliseconds 1500
}
$log.Add(("closed={0} via {1}" -f (-not ([Dd]::IsWindow($dlg) -and [Dd]::IsWindowVisible($dlg))), $Button))
if ($OutFile) { [IO.File]::WriteAllLines($OutFile, $log) }
$log
