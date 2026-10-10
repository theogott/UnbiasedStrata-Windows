$ErrorActionPreference='Stop'
$launcher=Join-Path $PSScriptRoot 'Start-DeepSeek41-Desktop.ps1'
if (-not (Test-Path -LiteralPath $launcher -PathType Leaf)) { throw 'Desktop launcher missing.' }
$desktop=[Environment]::GetFolderPath('Desktop')
$shortcutPath=Join-Path $desktop 'UnbiasedStrata - DeepSeek V4.1 Flash.lnk'
$shell=New-Object -ComObject WScript.Shell
$shortcut=$shell.CreateShortcut($shortcutPath)
$shortcut.TargetPath=Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
$shortcut.Arguments='-NoProfile -ExecutionPolicy Bypass -STA -File "'+$launcher+'"'
$shortcut.WorkingDirectory=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$shortcut.Description='Experimental native Windows DeepSeek V4.1 Flash server; select your GGUF model.'
$shortcut.IconLocation=$shortcut.TargetPath+',0'
$shortcut.Save()
Write-Output "Created desktop shortcut: $shortcutPath"
