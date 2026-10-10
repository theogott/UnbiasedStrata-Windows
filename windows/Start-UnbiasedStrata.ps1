param(
 [ValidateSet('IQ3_XXS','IQ3_S')][string]$Model='IQ3_XXS',
 [string]$DataDirectory=(Join-Path $PSScriptRoot '..\data'),
 [int]$Port=8080,
 [int]$Context=32768,
 [int]$MinimumFreeRamGiB=60,
 [switch]$CheckOnly
)
$ErrorActionPreference='Stop'
$binary=Join-Path $PSScriptRoot '..\bin\unbiased-strata.exe'
$modelFile=Join-Path $DataDirectory "models\$Model\Qwen3.8-Flash-Next-GSQ-RCO-$Model-00001-of-00002.gguf"
$shard2=$modelFile.Replace('-00001-of-00002','-00002-of-00002')
$pack=Join-Path $DataDirectory ('packs\'+$Model.ToLowerInvariant())
$draft=Join-Path $DataDirectory 'mtp\rt'
$keyFile=Join-Path $DataDirectory 'private\api-key.txt'
$required=@($binary,$modelFile,$shard2,(Join-Path $pack 'index.txt'),(Join-Path $pack 'dense.bin'),(Join-Path $pack 'native_experts.txt'),(Join-Path $pack 'expert-profile.bin'),(Join-Path $draft 'experts.bin'),(Join-Path $draft 'dense.bin'),(Join-Path $draft 'dense.txt'),(Join-Path $draft 'draft_vocab.bin'))
$missing=@($required | Where-Object { -not (Test-Path -LiteralPath $_ -PathType Leaf) })
$freeRam=[math]::Round((Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory/1MB,2)
if ($CheckOnly) { [pscustomobject]@{Model=$Model;FilesReady=($missing.Count -eq 0);Missing=$missing;FreeRamGiB=$freeRam;MinimumFreeRamGiB=$MinimumFreeRamGiB;InferenceTested=$false}; return }
if ($missing.Count) { throw ('Missing prepared files: '+($missing -join ', ')) }
if ($freeRam -lt $MinimumFreeRamGiB) { throw "Only $freeRam GiB RAM free; launcher requires $MinimumFreeRamGiB GiB. No model started." }
if ($Port -lt 1 -or $Port -gt 65535 -or $Context -lt 1024) { throw 'Invalid port or context' }
if (Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue) { throw "Port $Port already in use. Stop the other server first." }
if (-not (Test-Path -LiteralPath $keyFile)) {
 $keyParent=Split-Path $keyFile -Parent
 New-Item -ItemType Directory -Path $keyParent -Force | Out-Null
 $randomBytes=New-Object byte[] 32
 $random=[System.Security.Cryptography.RandomNumberGenerator]::Create()
 try { $random.GetBytes($randomBytes) } finally { $random.Dispose() }
 $key=([BitConverter]::ToString($randomBytes)).Replace('-','').ToLowerInvariant()
 [IO.File]::WriteAllText($keyFile,$key,[Text.Encoding]::ASCII)
 $acl=Get-Acl -LiteralPath $keyFile
 $acl.SetAccessRuleProtection($true,$false)
 $rule=New-Object System.Security.AccessControl.FileSystemAccessRule([System.Security.Principal.WindowsIdentity]::GetCurrent().Name,'FullControl','Allow')
 $acl.SetAccessRule($rule)
 Set-Acl -LiteralPath $keyFile -AclObject $acl
}
Write-Host "API http://127.0.0.1:$Port/v1; private key file: $keyFile"
Write-Host 'Experimental Windows port; real-model inference has not been validated. Ctrl+C stops this foreground server.'
& $binary --model $modelFile --pack $pack --mtp $draft --api-key-file $keyFile --host 127.0.0.1 --port $Port --ctx $Context --threads 4 -- --vram-reserve-mib 4096 --kv int8
exit $LASTEXITCODE
