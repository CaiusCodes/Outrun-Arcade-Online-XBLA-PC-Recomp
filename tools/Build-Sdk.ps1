[CmdletBinding()]
param([string]$SdkDirectory,[string]$InstallDirectory,[ValidateRange(1,32)][int]$Jobs=6,[switch]$Vulkan=$true)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
if(-not $SdkDirectory){$SdkDirectory=Join-Path $root 'out/dependencies/rexglue-sdk'}
if(-not $InstallDirectory){$InstallDirectory=Join-Path $root 'out/sdk-install'}
$SdkDirectory=[IO.Path]::GetFullPath($SdkDirectory)
$InstallDirectory=[IO.Path]::GetFullPath($InstallDirectory)
$build=Join-Path $SdkDirectory 'out/build/win-amd64'
$vulkanOption=if($Vulkan){'ON'}else{'OFF'}
$sdkPython=(& python -c 'import sys; print(sys.executable)').Trim()
if($LASTEXITCODE -ne 0){throw 'Python interpreter was not found'}

& cmake -S $SdkDirectory -B $build -G 'Ninja Multi-Config' `
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ `
  '-DCMAKE_C_FLAGS=-march=x86-64-v3' '-DCMAKE_CXX_FLAGS=-march=x86-64-v3' `
  '-DCMAKE_CONFIGURATION_TYPES=Debug;Release;RelWithDebInfo' `
  "-DCMAKE_INSTALL_PREFIX=$InstallDirectory" `
  -DCMAKE_EXPORT_NO_PACKAGE_REGISTRY=ON `
  "-DPYTHON_EXECUTABLE=$sdkPython" "-DPython3_EXECUTABLE=$sdkPython" `
  -DREXGLUE_USE_D3D12=ON "-DREXGLUE_USE_VULKAN=$vulkanOption" -DREXGLUE_BUILD_TESTS=OFF
if($LASTEXITCODE -ne 0){throw 'SDK configure failed'}
& cmake --build $build --config Release --parallel $Jobs
if($LASTEXITCODE -ne 0){throw 'SDK build failed'}
& cmake --install $build --config Release
if($LASTEXITCODE -ne 0){throw 'SDK install failed'}
Write-Host "Outrun SDK installed: $InstallDirectory"
