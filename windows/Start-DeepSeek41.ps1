param(
 [Parameter(Mandatory=$true)][string]$ModelFile,
 [Parameter(Mandatory=$true)][string]$ApiKeyFile,
 [ValidateRange(1,65535)][int]$Port=8080,
 [ValidateRange(1024,262144)][int]$Context=4096,
 [ValidateRange(1,64)][int]$Threads=4,
 [ValidateRange(1,22)][int]$ExpertVramGiB=18,
 [ValidateRange(0,72)][int]$ExpertRamGiB=64,
 [ValidateRange(1,16)][int]$IoThreads=4,
 [switch]$CheckOnly
)
$ErrorActionPreference='Stop'
$frontend=Join-Path $PSScriptRoot '..\bin\unbiased-strata.exe'
if (-not (Test-Path -LiteralPath $frontend -PathType Leaf)) { throw 'Build/install the patched Windows frontend first.' }
if (-not $CheckOnly -and (Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue)) { throw "Port $Port already in use." }
$arguments=@('--backend','deepseek41','--model',$ModelFile,'--api-key-file',$ApiKeyFile,'--port',"$Port",'--ctx',"$Context",'--threads',"$Threads",'--expert-vram-gib',"$ExpertVramGiB",'--expert-ram-gib',"$ExpertRamGiB",'--io-threads',"$IoThreads")
if ($CheckOnly) { $arguments+='--check-only' }
& $frontend @arguments
if ($LASTEXITCODE -eq 3 -and $CheckOnly) { Write-Host 'Files and metadata checked; insufficient free RAM for the selected cache. No model started.' }
exit $LASTEXITCODE
