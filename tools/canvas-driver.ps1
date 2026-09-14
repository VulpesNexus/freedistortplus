<#
.SYNOPSIS
    Posts a mouse gesture to Illustrator's document view window: press at one
    point, move through steps, release at another.

.DESCRIPTION
    Coordinates are the view's own, as AIDocumentViewSuite reports them (the
    bridge's "editor handles" selector), which are client pixels of the
    view's "OS_ViewContainer" window. Posted messages do not move the real
    pointer or take the keyboard, so this can run while someone else is using
    the machine; whether Illustrator's tool dispatch accepts them is what the
    probe that calls it finds out.

    -Keys holds modifier flags for the mouse messages' wParam: 4 for Shift
    (MK_SHIFT), 8 for Ctrl (MK_CONTROL). Alt is not a wParam flag and cannot be
    posted this way.
#>
param(
    [Parameter(Mandatory)] [int] $X1,
    [Parameter(Mandatory)] [int] $Y1,
    [Parameter(Mandatory)] [int] $X2,
    [Parameter(Mandatory)] [int] $Y2,
    [int] $Steps = 12,
    [int] $Keys = 0,
    [switch] $Escape
)

Add-Type @"
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class Canvas {
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr p, EnumProc cb, IntPtr l);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern IntPtr GetParent(IntPtr h);
    // The document view: the "OS_ViewContainer" whose parent is an
    // "OWL.Document" window. Panels are Drover views too, with the same
    // title -- the Tools panel is the first one found -- so the title alone
    // picks the wrong window.
    public static IntPtr View(IntPtr main) {
        IntPtr hit = IntPtr.Zero;
        EnumChildWindows(main, delegate(IntPtr h, IntPtr x) {
            var s = new StringBuilder(64); GetWindowTextW(h, s, 64);
            if (!IsWindowVisible(h) || s.ToString() != "OS_ViewContainer") return true;
            var c = new StringBuilder(64); GetClassNameW(GetParent(h), c, 64);
            if (c.ToString() == "OWL.Document") { hit = h; return false; }
            return true;
        }, IntPtr.Zero);
        return hit;
    }
}
"@

$main = (Get-Process Illustrator | Select-Object -First 1).MainWindowHandle
$view = [Canvas]::View($main)
if ($view -eq [IntPtr]::Zero) { 'no document view window'; return }
$lp = { param($x, $y) [IntPtr] ((([int] $y) -shl 16) -bor (([int] $x) -band 0xFFFF)) }
$wm = @{ Move = 0x0200; Down = 0x0201; Up = 0x0202; KeyDown = 0x0100; KeyUp = 0x0101 }

[Canvas]::PostMessage($view, $wm.Move, [IntPtr] $Keys, (& $lp $X1 $Y1)) | Out-Null
Start-Sleep -Milliseconds 150
[Canvas]::PostMessage($view, $wm.Down, [IntPtr] (1 -bor $Keys), (& $lp $X1 $Y1)) | Out-Null
Start-Sleep -Milliseconds 200
for ($i = 1; $i -le $Steps; $i++) {
    $x = $X1 + ($X2 - $X1) * $i / $Steps
    $y = $Y1 + ($Y2 - $Y1) * $i / $Steps
    [Canvas]::PostMessage($view, $wm.Move, [IntPtr] (1 -bor $Keys), (& $lp $x $y)) | Out-Null
    Start-Sleep -Milliseconds 60
    if ($Escape -and $i -eq [int] ($Steps / 2)) {
        [Canvas]::PostMessage($view, $wm.KeyDown, [IntPtr] 0x1B, [IntPtr] 0) | Out-Null
        [Canvas]::PostMessage($view, $wm.KeyUp, [IntPtr] 0x1B, [IntPtr] 0) | Out-Null
        Start-Sleep -Milliseconds 100
    }
}
[Canvas]::PostMessage($view, $wm.Up, [IntPtr] $Keys, (& $lp $X2 $Y2)) | Out-Null
Start-Sleep -Milliseconds 400
"posted to view $view : ($X1,$Y1) -> ($X2,$Y2) in $Steps steps, keys $Keys"
