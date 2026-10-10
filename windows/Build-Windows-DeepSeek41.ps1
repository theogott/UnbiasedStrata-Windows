param(
 [Parameter(Mandatory=$true)][string]$CudaToolkitPath,
 [string]$Python='python',
 [string]$BuildDirectory=(Join-Path $PSScriptRoot '..\backends\deepseek41\build-sm120'),
 [ValidateRange(1,32)][int]$Jobs=4
)
$ErrorActionPreference='Stop'
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) { throw 'Run from an x64 Visual Studio developer PowerShell.' }
$root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$nvcc=Join-Path $CudaToolkitPath 'bin\nvcc.exe'
$version=(& $nvcc --version | Out-String)
if ($LASTEXITCODE -ne 0 -or $version -notmatch 'V(\d+)\.(\d+)\.(\d+)') { throw 'Cannot determine CUDA compiler version.' }
if ([int]$Matches[1] -lt 13 -or ([int]$Matches[1] -eq 13 -and [int]$Matches[2] -eq 2 -and [int]$Matches[3] -lt 86)) { throw 'Use CUDA 13+; CUDA 13.2 must be build 86 or newer.' }
& $Python (Join-Path $PSScriptRoot 'Prepare-DeepSeek41.py')
if ($LASTEXITCODE -ne 0) { throw 'Pinned source preparation failed.' }
$pin=Get-Content -Raw (Join-Path $root 'backends\deepseek41\source-pin.json') | ConvertFrom-Json
$env:PATH=(Join-Path $CudaToolkitPath 'bin')+';'+$env:PATH
& cmake -S (Join-Path $root 'backends\deepseek41\source') -B $BuildDirectory -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=cl -DCMAKE_C_COMPILER=cl "-DCMAKE_CUDA_COMPILER=$nvcc" -DGGML_CUDA=ON -DCMAKE_CUDA_ARCHITECTURES=120 -DGGML_NATIVE=OFF -DBUILD_SHARED_LIBS=OFF -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded -DLLAMA_BUILD_TESTS=OFF -DLLAMA_BUILD_EXAMPLES=OFF -DLLAMA_BUILD_APP=OFF -DLLAMA_BUILD_UI=OFF -DLLAMA_OPENSSL=OFF -DLLAMA_SUBPROCESS=OFF "-DLLAMA_BUILD_COMMIT=$($pin.commit.Substring(0,8))"
if ($LASTEXITCODE -ne 0) { throw 'DeepSeek CMake configure failed.' }
& cmake --build $BuildDirectory --target llama-server unbiased-backend-test -j $Jobs
if ($LASTEXITCODE -ne 0) { throw 'DeepSeek Windows build failed.' }
& (Join-Path $BuildDirectory 'bin\unbiased-backend-test.exe')
if ($LASTEXITCODE -ne 0) { throw 'DeepSeek model-free tests failed.' }
$bin=Join-Path $root 'bin'; New-Item -ItemType Directory -Path $bin -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $BuildDirectory 'bin\llama-server.exe') -Destination (Join-Path $bin 'unbiased-deepseek-server.exe')
foreach ($name in @('cudart64_13.dll','cublas64_13.dll','cublasLt64_13.dll')) { Copy-Item -LiteralPath (Join-Path $CudaToolkitPath "bin\x64\$name") -Destination (Join-Path $bin $name) }
Write-Host "Built DeepSeek-V4.1 Windows backend from $($pin.commit). GPU/model inference remains untested."
