[CmdletBinding()]
param([string]$SdkRoot, [string]$BuildDirectory, [switch]$Generate, [ValidateRange(1,32)][int]$Jobs = 4)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $SdkRoot) { $SdkRoot = Join-Path $projectRoot 'out\sdk-install' }
$SdkRoot = (Resolve-Path -LiteralPath $SdkRoot).Path
$generator = Join-Path $SdkRoot 'bin\rexglue.exe'
if (-not (Test-Path -LiteralPath $generator)) { throw 'Set -SdkRoot or OUTRUN_REXSDK to the installed ReXGlue 0.9.0 SDK.' }
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw 'Visual Studio C++ build tools were not found.' }
Import-Module (Join-Path $vsPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
$ninja = Get-Command ninja -ErrorAction SilentlyContinue
if (-not $ninja) {
    $ninjaPath = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
    if (-not (Test-Path -LiteralPath $ninjaPath)) {
        $ninjaPath = Get-ChildItem -Path (Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet\Packages\Ninja-build.Ninja_*\ninja.exe') | Select-Object -First 1 -ExpandProperty FullName
    }
} else { $ninjaPath = $ninja.Source }
if (-not $ninjaPath) { throw 'Ninja build tool was not found.' }
Push-Location $projectRoot
try {
    New-Item -ItemType Directory -Path out -Force | Out-Null
    if ($Generate -or -not (Test-Path generated\default\sources.cmake)) {
        if (-not (Test-Path private\assets\default.xex)) { throw 'Run Import Game.bat first.' }
        & $generator --log-file out/codegen.log codegen outrun_manifest.toml
        if ($LASTEXITCODE -ne 0) { throw 'ReXGlue code generation failed. See out/codegen.log.' }
    }
    if($BuildDirectory) {
        $buildPath=[IO.Path]::GetFullPath((Join-Path $projectRoot $BuildDirectory))
        & cmake -S $projectRoot -B $buildPath -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ "-DOUTRUN_SDK_PREFIX=$SdkRoot" "-DCMAKE_MAKE_PROGRAM=$ninjaPath"
    } else {& cmake --preset win-amd64-release "-DOUTRUN_SDK_PREFIX=$SdkRoot" "-DCMAKE_MAKE_PROGRAM=$ninjaPath"}
    if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
    if($BuildDirectory){& cmake --build $buildPath --parallel $Jobs}
    else {& cmake --build --preset win-amd64-release --parallel $Jobs}
    if ($LASTEXITCODE -ne 0) { throw 'Native compilation failed.' }
} finally { Pop-Location }
