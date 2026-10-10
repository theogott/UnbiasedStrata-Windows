param([switch]$VerifyOnly)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$starter=Join-Path $PSScriptRoot 'Start-DeepSeek41.ps1'
$binary=Join-Path $root 'bin\unbiased-strata.exe'
$backend=Join-Path $root 'bin\unbiased-deepseek-server.exe'
foreach ($path in @($starter,$binary,$backend)) {
 if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Required file missing: $path" }
}
if ($VerifyOnly) {
 Write-Output 'Desktop launcher paths verified. No model or GPU started.'
 return
}
try {
 Write-Host 'UnbiasedStrata Windows - DeepSeek V4.1 Flash (experimental)' -ForegroundColor Cyan
 Write-Host 'Select the first shard of your downloaded DeepSeek-V4.1-Flash GGUF model.'
 Write-Host 'Model weights are not included. Real inference has not yet been verified.'
 Write-Host 'The default cache requires at least 96 GiB of free RAM; free GPU memory is also needed.'
 Add-Type -AssemblyName System.Windows.Forms
 $dialog=New-Object System.Windows.Forms.OpenFileDialog
 $dialog.Title='Select DeepSeek V4.1 Flash: first GGUF shard (00001)'
 $dialog.Filter='GGUF model (*.gguf)|*.gguf'
 $dialog.CheckFileExists=$true
 try {
  if ($dialog.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) { return }
  $model=$dialog.FileName
 } finally { $dialog.Dispose() }
 $private=Join-Path $root 'data\private'
 $keyFile=Join-Path $private 'deepseek-api-key.txt'
 if (-not (Test-Path -LiteralPath $keyFile -PathType Leaf)) {
  New-Item -ItemType Directory -Path $private -Force | Out-Null
  $item=Get-Item -LiteralPath $private
  if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Private key directory must not be a link.' }
  $acl=Get-Acl -LiteralPath $private
  $acl.SetAccessRuleProtection($true,$false)
  $sid=[System.Security.Principal.WindowsIdentity]::GetCurrent().User
  $rule=New-Object System.Security.AccessControl.FileSystemAccessRule($sid,'FullControl','ContainerInherit,ObjectInherit','None','Allow')
  $acl.SetAccessRule($rule)
  Set-Acl -LiteralPath $private -AclObject $acl
  $bytes=New-Object byte[] 32
  $rng=[Security.Cryptography.RandomNumberGenerator]::Create()
  try { $rng.GetBytes($bytes) } finally { $rng.Dispose() }
  $key=([BitConverter]::ToString($bytes)).Replace('-','').ToLowerInvariant()
  [IO.File]::WriteAllText($keyFile,$key,[Text.Encoding]::ASCII)
 }
 Write-Host "Local API: http://127.0.0.1:8080/v1; key file: $keyFile"
 Write-Host 'Ctrl+C stops the server. Keep this window open while it runs.'
 & "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File $starter -ModelFile $model -ApiKeyFile $keyFile
 if ($LASTEXITCODE -ne 0) { throw "Server exited with code $LASTEXITCODE. See the message above." }
} catch {
 Write-Host $_.Exception.Message -ForegroundColor Red
 [void](Read-Host 'Press Enter to close')
}
