Add-Type -AssemblyName System.Drawing
$root = 'E:\rgss2-rebuild\test'
foreach ($d in @('diff_orig','diff_rebuild','diff_out','diff_orig2','diff_rebuild2')) {
  $dir = Join-Path $root $d
  if (-not (Test-Path $dir)) { continue }
  Get-ChildItem -Path $dir -Filter *.bmp | ForEach-Object {
    $png = [System.IO.Path]::ChangeExtension($_.FullName, '.png')
    $img = [System.Drawing.Image]::FromFile($_.FullName)
    $img.Save($png, [System.Drawing.Imaging.ImageFormat]::Png)
    $img.Dispose()
    Write-Output ("$d/$($_.Name) -> ok")
  }
}
