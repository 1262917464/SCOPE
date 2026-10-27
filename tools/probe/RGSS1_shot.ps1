param([string]$Out = 'E:\rgss2-rebuild\unpacked\RGSS1_m11_shot.png', [int]$Wait = 12)
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public class Win {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
    public struct RECT { public int L, T, R, B; }
}
'@
Start-Sleep -Seconds $Wait
$p = Get-Process Game -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
if (-not $p) { Write-Host "NO-WINDOW: Game.exe not found or no main window"; exit 1 }
[Win]::ShowWindow($p.MainWindowHandle, 5) | Out-Null
[Win]::SetForegroundWindow($p.MainWindowHandle) | Out-Null
Start-Sleep -Milliseconds 1200
$r = New-Object Win+RECT
[Win]::GetWindowRect($p.MainWindowHandle, [ref]$r) | Out-Null
$w = $r.R - $r.L; $h = $r.B - $r.T
Write-Host ("title=[" + $p.MainWindowTitle + "] rect=" + $r.L + "," + $r.T + " size=" + $w + "x" + $h)
$bmp = New-Object System.Drawing.Bitmap($w, $h)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.L, $r.T, 0, 0, $bmp.Size)
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
Write-Host ("saved " + $Out)
