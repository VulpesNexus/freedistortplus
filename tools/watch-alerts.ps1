<#
.SYNOPSIS
    Records, captures, and dismisses every modal window Illustrator raises for
    a while -- run as a background job around a call that might raise one.

.DESCRIPTION
    A modal blocks the scripting call that raised it, so nothing in the probe's
    own process can see or dismiss it. This watches from outside: any visible
    top-level #32770 window of Illustrator's process that was not there when
    it started is logged with its title, captured to PNG when -ShotPrefix is
    given, and closed with Enter.

    The point for this project is the negative: opening a document edited with
    this plugin, on an Illustrator without it, should raise nothing at all.
#>
param(
    [int] $Seconds = 60,
    [string] $ShotPrefix = ''
)

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class AlertWatch {
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    public static string Cls(IntPtr h) { var s = new StringBuilder(128); GetClassNameW(h, s, 128); return s.ToString(); }
    public static string Txt(IntPtr h) { var s = new StringBuilder(512); GetWindowTextW(h, s, 512); return s.ToString(); }
    public static List<IntPtr> Dialogs() {
        var list = new List<IntPtr>();
        EnumWindows(delegate(IntPtr h, IntPtr l) {
            uint pid;
            GetWindowThreadProcessId(h, out pid);
            if (IsWindowVisible(h) && Cls(h) == "#32770") {
                try { if (System.Diagnostics.Process.GetProcessById((int) pid).ProcessName == "Illustrator") list.Add(h); } catch { }
            }
            return true;
        }, IntPtr.Zero);
        return list;
    }
}
"@

$seen = @{}
foreach ($h in [AlertWatch]::Dialogs()) { $seen[$h.ToInt64()] = $true }
$count = 0
$deadline = (Get-Date).AddSeconds($Seconds)
while ((Get-Date) -lt $deadline) {
    foreach ($h in [AlertWatch]::Dialogs()) {
        if ($seen.ContainsKey($h.ToInt64())) { continue }
        $seen[$h.ToInt64()] = $true
        $count++
        Start-Sleep -Milliseconds 600
        "alert $count`: title '$([AlertWatch]::Txt($h))'"
        if ($ShotPrefix) {
            $r = New-Object AlertWatch+RECT
            [AlertWatch]::GetWindowRect($h, [ref] $r) | Out-Null
            $bmp = New-Object Drawing.Bitmap ([math]::Max(1, $r.R - $r.L)), ([math]::Max(1, $r.B - $r.T))
            $g = [Drawing.Graphics]::FromImage($bmp)
            $hdc = $g.GetHdc()
            [AlertWatch]::PrintWindow($h, $hdc, 2) | Out-Null
            $g.ReleaseHdc($hdc); $g.Dispose()
            $bmp.Save("$ShotPrefix-$count.png", [Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
        }
        [AlertWatch]::PostMessage($h, 0x0100, [IntPtr] 0x0D, [IntPtr]::Zero) | Out-Null
        [AlertWatch]::PostMessage($h, 0x0101, [IntPtr] 0x0D, [IntPtr]::Zero) | Out-Null
    }
    Start-Sleep -Milliseconds 300
}
"alerts: $count"
