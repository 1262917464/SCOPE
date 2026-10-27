param([string]$Keys = 'ENTER', [int]$WaitAfter = 6)
Add-Type @'
using System;
using System.Runtime.InteropServices;
public class PK {
    [DllImport("user32.dll", SetLastError=true)] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
}
'@
$p = Get-Process Game -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
if (-not $p) { Write-Host "NO GAME"; exit 1 }
$h = $p.MainWindowHandle
Write-Host ("target hwnd=" + $h)
$WM_KEYDOWN = 0x0100; $WM_KEYUP = 0x0101; $WM_CHAR = 0x0102
$vks = @{ 'ENTER' = @(0x0D, 13); 'Z' = @(0x5A, 122); 'X' = @(0x58, 120); 'DOWN' = @(0x28, 0); 'LEFT' = @(0x25, 0); 'RIGHT' = @(0x27, 0); 'SPACE' = @(0x20, 32); 'ESC' = @(0x1B, 27) }
foreach ($k in $Keys.Split(',')) {
    $n = $k.Trim().ToUpper()
    if (-not $vks.ContainsKey($n)) { continue }
    $vk = [byte]$vks[$n][0]; $ch = [int]$vks[$n][1]
    $lp = [IntPtr](1 -bor ($vk -shl 16))
    [PK]::PostMessage($h, $WM_KEYDOWN, [IntPtr]$vk, $lp) | Out-Null
    Start-Sleep -Milliseconds 120
    if ($ch -gt 0) { [PK]::PostMessage($h, $WM_CHAR, [IntPtr]$ch, $lp) | Out-Null }
    [PK]::PostMessage($h, $WM_KEYUP, [IntPtr]$vk, [IntPtr](0xC0000001 -bor ($vk -shl 16))) | Out-Null
    Write-Host ("posted " + $n)
    Start-Sleep -Milliseconds 700
}
Start-Sleep -Seconds $WaitAfter
Write-Host "post done"
