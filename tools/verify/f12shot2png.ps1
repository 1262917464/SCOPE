Add-Type -AssemblyName System.Drawing
$dir = 'E:\rgss2-rebuild\local\test\f12shots'
Get-ChildItem -Path $dir -Filter *.bmp | ForEach-Object {
  $png = [System.IO.Path]::ChangeExtension($_.FullName, '.png')
  $img = [System.Drawing.Image]::FromFile($_.FullName)
  $img.Save($png, [System.Drawing.Imaging.ImageFormat]::Png)
  $img.Dispose()
  Write-Output ("ok " + $_.Name)
}
