param([string]$Keys = 'ENTER', [int]$WaitAfter = 6)
Add-Type @'
using System;
using System.Runtime.InteropServices;
public class FI {
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, IntPtr pid);
    [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
    [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint a, uint b, bool f);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr h);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, IntPtr extra);
    [DllImport("user32.dll")] public static extern IntPtr SetFocus(IntPtr h);
}
'@
$p = Get-Process Game -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
if (-not $p) { Write-Host "NO GAME"; exit 1 }
$h = $p.MainWindowHandle
[FI]::ShowWindow($h, 9) | Out-Null
$fg = [FI]::GetForegroundWindow()
$fgt = [FI]::GetWindowThreadProcessId($fg, [IntPtr]::Zero)
$myt = [FI]::GetCurrentThreadId()
$gameT = [FI]::GetWindowThreadProcessId($h, [IntPtr]::Zero)
[FI]::AttachThreadInput($myt, $fgt, $true) | Out-Null
[FI]::AttachThreadInput($myt, $gameT, $true) | Out-Null
[FI]::BringWindowToTop($h) | Out-Null
[FI]::SetForegroundWindow($h) | Out-Null
[FI]::SetFocus($h) | Out-Null
Start-Sleep -Milliseconds 900
$ok = ([FI]::GetForegroundWindow() -eq $h)
Write-Host ("foreground_ok=" + $ok)
if (-not $ok) { Write-Host "WARN: focus not acquired; keys may not reach game" }
$vks = @{ 'ENTER' = @(0x0D); 'Z' = @(0x5A); 'X' = @(0x58); 'DOWN' = @(0x28); 'LEFT' = @(0x25); 'RIGHT' = @(0x27); 'SPACE' = @(0x20); 'ESC' = @(0x1B) }
foreach ($k in $Keys.Split(',')) {
    $n = $k.Trim().ToUpper()
    if (-not $vks.ContainsKey($n)) { continue }
    $vk = [byte]$vks[$n][0]
    [FI]::keybd_event($vk, 0, 0, [IntPtr]::Zero); Start-Sleep -Milliseconds 130
    [FI]::keybd_event($vk, 0, 2, [IntPtr]::Zero); Write-Host ("sent " + $n)
    Start-Sleep -Milliseconds 700
}
Start-Sleep -Seconds $WaitAfter
[FI]::AttachThreadInput($myt, $fgt, $false) | Out-Null
[FI]::AttachThreadInput($myt, $gameT, $false) | Out-Null
Write-Host "input done"
