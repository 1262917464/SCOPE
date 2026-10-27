param([string]$Keys = '{ENTER}', [int]$Wait = 6)
Add-Type @'
using System;
using System.Runtime.InteropServices;
public class K {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, IntPtr extra);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
}
'@
$p = Get-Process Game -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
if (-not $p) { Write-Host "NO GAME"; exit 1 }
[K]::ShowWindow($p.MainWindowHandle, 5) | Out-Null
[K]::SetForegroundWindow($p.MainWindowHandle) | Out-Null
Start-Sleep -Milliseconds 700
Write-Host ("foreground=" + ([K]::GetForegroundWindow() -eq $p.MainWindowHandle) + " target=" + $p.MainWindowHandle)
# VK_RETURN=0x0D, VK_Z=0x5A, VK_DOWN=0x28, VK_LEFT=0x25, VK_RIGHT=0x27, VK_SPACE=0x20
$vks = @{ 'ENTER' = 0x0D; 'Z' = 0x5A; 'DOWN' = 0x28; 'LEFT' = 0x25; 'RIGHT' = 0x27; 'SPACE' = 0x20 }
foreach ($k in $Keys.Split(',')) {
    $name = $k.Trim().ToUpper()
    if (-not $vks.ContainsKey($name)) { continue }
    $vk = [byte]$vks[$name]
    [K]::keybd_event($vk, 0, 0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 90
    [K]::keybd_event($vk, 0, 2, [IntPtr]::Zero)   # KEYEVENTF_KEYUP
    Write-Host ("sent " + $name)
    Start-Sleep -Milliseconds 500
}
Start-Sleep -Seconds $Wait
Write-Host "input done"
