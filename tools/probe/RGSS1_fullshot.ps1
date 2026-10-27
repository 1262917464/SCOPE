Add-Type -AssemblyName System.Windows.Forms, System.Drawing
$vs = [System.Windows.Forms.SystemInformation]::VirtualScreen
Write-Host ("virtual screen: " + $vs.X + "," + $vs.Y + " " + $vs.Width + "x" + $vs.Height)
$bmp = New-Object System.Drawing.Bitmap($vs.Width, $vs.Height)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($vs.X, $vs.Y, 0, 0, $bmp.Size)
$bmp.Save('E:\rgss2-rebuild\unpacked\RGSS1_fullscreen.png', [System.Drawing.Imaging.ImageFormat]::Png)
Write-Host "saved fullscreen"
$p = Get-Process Game -ErrorAction SilentlyContinue | Select-Object -First 1
if ($p) { Write-Host ("Game.exe pid=" + $p.Id + " responding=" + $p.Responding + " title=[" + $p.MainWindowTitle + "]") }
