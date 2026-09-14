<#
.SYNOPSIS
    Captures Illustrator's document view to a PNG with PrintWindow, without
    bringing Illustrator forward or touching the mouse or keyboard.

.DESCRIPTION
    PrintWindow asks the window to paint itself into a bitmap, so the capture
    works while Illustrator is behind other windows. Whether Illustrator's
    GPU-drawn canvas paints into it at all is part of what a capture shows:
    a blank image means this route does not work for that view mode.
#>
param([Parameter(Mandatory)] [string] $Path)

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class ViewCapture {
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr p, EnumProc cb, IntPtr l);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
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
[ViewCapture]::SetProcessDPIAware() | Out-Null
$main = (Get-Process Illustrator | Select-Object -First 1).MainWindowHandle
$view = [ViewCapture]::View($main)
if ($view -eq [IntPtr]::Zero) { 'no document view'; return }
$r = New-Object ViewCapture+RECT
[ViewCapture]::GetClientRect($view, [ref] $r) | Out-Null
$bmp = New-Object Drawing.Bitmap ([math]::Max(1, $r.R)), ([math]::Max(1, $r.B))
$g = [Drawing.Graphics]::FromImage($bmp)
$hdc = $g.GetHdc()
$ok = [ViewCapture]::PrintWindow($view, $hdc, 2)
$g.ReleaseHdc($hdc); $g.Dispose()
$bmp.Save($Path, [Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
"captured {0}x{1}, PrintWindow {2}" -f $r.R, $r.B, $ok
