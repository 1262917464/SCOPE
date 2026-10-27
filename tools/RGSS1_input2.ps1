param([string]$Keys = 'ENTER', [int]$WaitAfter = 8)
Add-Type -AssemblyName Microsoft.VisualBasic
Add-Type @'
using System;
using System.Runtime.InteropServices;
public class K2 {
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, IntPtr extra);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
    [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr h);
}
'@
$p = Get-Process Game -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
if (-not $p) { Write-Host "NO GAME"; exit 1 }
$h = $p.MainWindowHandle
[K2]::ShowWindow($h, 9) | Out-Null          # SW_RESTORE
[Microsoft.VisualBasic.Interaction]::AppActivate($p.Id) | Out-Null
Start-Sleep -Milliseconds 900
$fg = [K2]::GetForegroundWindow()
if ($fg -ne $h) {
    [K2]::BringWindowToTop($h) | Out-Null
    [K2]::SetForegroundWindow($h) | Out-Null
    Start-Sleep -Milliseconds 700
    $fg = [K2]::GetForegroundWindow()
}
Write-Host ("foreground_ok=" + ($fg -eq $h))
$vks = @{ 'ENTER' = 0x0D; 'Z' = 0x5A; 'X' = 0x58; 'DOWN' = 0x28; 'LEFT' = 0x25; 'RIGHT' = 0x27; 'SPACE' = 0x20; 'ESC' = 0x1B }
foreach ($k in $Keys.Split(',')) {
    $n = $k.Trim().ToUpper()
    if (-not $vks.ContainsKey($n)) { continue }
    $vk = [byte]$vks[$n]
    [K2]::keybd_event($vk, 0, 0, [IntPtr]::Zero); Start-Sleep -Milliseconds 110
    [K2]::keybd_event($vk, 0, 2, [IntPtr]::Zero); Write-Host ("sent " + $n)
    Start-Sleep -Milliseconds 600
}
Start-Sleep -Seconds $WaitAfter
Write-Host "input done"
