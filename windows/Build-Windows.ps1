param(
 [Parameter(Mandatory=$true)][string]$CudaToolkitPath,
 [string]$BuildDirectory=(Join-Path $PSScriptRoot '..\build-windows'),
 [int]$Jobs=4
)
$ErrorActionPreference='Stop'
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) { throw 'Run this from an x64 Visual Studio developer PowerShell.' }
$nvcc=Join-Path $CudaToolkitPath 'bin\nvcc.exe'
$version=(& $nvcc --version | Out-String)
if ($LASTEXITCODE -ne 0 -or $version -notmatch 'V(\d+)\.(\d+)\.(\d+)') { throw 'Cannot determine CUDA compiler version.' }
$major=[int]$Matches[1]; $minor=[int]$Matches[2]; $build=[int]$Matches[3]
if ($major -lt 13 -or ($major -eq 13 -and $minor -eq 2 -and $build -lt 86)) { throw 'RTX 50 needs CUDA 13+; CUDA 13.2 builds before 86 have a known correctness defect.' }
if ($Jobs -lt 1 -or $Jobs -gt 32) { throw 'Jobs must be between 1 and 32.' }
$source=Resolve-Path (Join-Path $PSScriptRoot '..')
if (-not (Test-Path -LiteralPath (Join-Path $source 'third_party\llama.cpp\ggml\CMakeLists.txt'))) { throw 'Initialize the pinned submodule with git submodule update --init.' }
$env:PATH=(Join-Path $CudaToolkitPath 'bin')+';'+$env:PATH
& cmake -S $source -B $BuildDirectory -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_CUDA_COMPILER=$nvcc" -DSTRATA_ENABLE_CUDA=ON -DSTRATA_NATIVE_EXPERTS=ON -DSTRATA_PORTABLE=ON -DSTRATA_BUILD_TESTS=OFF -DCMAKE_CUDA_ARCHITECTURES=120 -DBUILD_SHARED_LIBS=OFF -DSTRATA_WINDOWS_TESTS=ON
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
& cmake --build $BuildDirectory -j $Jobs --target unbiased-strata windows-process-test windows-http-test windows-deepseek-test
if ($LASTEXITCODE -ne 0) { throw 'Windows build failed.' }
& ctest --test-dir $BuildDirectory -R windows- --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Model-free Windows tests failed.' }
Write-Host 'Build and model-free checks complete. GPU inference has not been tested.'
