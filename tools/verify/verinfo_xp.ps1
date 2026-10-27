foreach ($f in 'RGSS102J','RGSS103J','RGSS104E') {
  $v = (Get-Item "E:\rgss2-rebuild\backup\$f.dll").VersionInfo
  Write-Host "== $f"
  Write-Host ("  FileVersion   = " + $v.FileVersion)
  Write-Host ("  ProductVersion= " + $v.ProductVersion)
  Write-Host ("  Description   = " + $v.FileDescription)
  Write-Host ("  Company       = " + $v.CompanyName)
  Write-Host ("  Product       = " + $v.ProductName)
  Write-Host ("  OrigFilename  = " + $v.OriginalFilename)
}
