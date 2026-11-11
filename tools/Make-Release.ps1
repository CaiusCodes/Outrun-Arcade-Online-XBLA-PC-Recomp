# Release ZIP for OutRun Online Arcade PC (portable layout, no game files).
#
#   powershell -ExecutionPolicy Bypass -File tools\Make-Release.ps1 [-BuildDir out\build\win-amd64-release]
#       [-RuntimeDir <folder with the tested rexruntime.dll, rexgpu-xenos.dll, TracyClient.dll>] [-OutDir out\release]
#
# Takes outrun.exe, native_pipelines.bin, hud\ and DXC from the build folder,
# the runtime DLLs from -RuntimeDir (default: the build folder), compiles the
# launcher from setup\ with the .NET Framework compiler, neutralises any
# build-machine paths left in the binaries (tools\Scrub-BuildPaths.py), refuses
# to package game files, settings, saves or user paths, and writes
# <OutDir>\Outrun-Arcade-Online-XBLA-PC-Recomp-v<VERSION>.zip (the name Check for
# Updates looks for) with its .sha256.
param(
  [string]$BuildDir = 'out\build\win-amd64-release',
  [string]$RuntimeDir = '',
  [string]$OutDir = 'out\release'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
$version = (Get-Content -Raw (Join-Path $root 'VERSION')).Trim()
$build = (Resolve-Path $BuildDir).Path
$runtime = if ($RuntimeDir) { (Resolve-Path $RuntimeDir).Path } else { $build }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$out = (Resolve-Path $OutDir).Path
$stage = Join-Path $out 'OutRun Online Arcade'
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
$res = Join-Path $stage 'resources'
New-Item -ItemType Directory -Path $res | Out-Null

# Game program, native renderer pieces and HUD art from the build.
foreach ($n in 'outrun.exe', 'native_pipelines.bin', 'dxcompiler.dll', 'dxil.dll') { Copy-Item (Join-Path $build $n) $res }
Copy-Item (Join-Path $build 'hud') (Join-Path $res 'hud') -Recurse
foreach ($n in 'rexruntime.dll', 'rexgpu-xenos.dll', 'TracyClient.dll') { Copy-Item (Join-Path $runtime $n) $res }
$exeVersion = (Get-Item (Join-Path $res 'outrun.exe')).VersionInfo.ProductVersion
if (-not $exeVersion.StartsWith($version)) { throw "outrun.exe is version $exeVersion, VERSION says $version" }
Copy-Item (Join-Path $root 'packaging\outrun.toml') (Join-Path $res 'defaults.toml')
Copy-Item (Join-Path $root 'packaging\resources\outrun.ico') $res
Copy-Item (Join-Path $root 'packaging\PORTABLE-README.txt') (Join-Path $stage 'README.txt')
Copy-Item (Join-Path $root 'packaging\licenses') $stage -Recurse

# Launcher (importer, setup and updater).
$compiler = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
$sources = @('StfsPackage.cs', 'GameValidation.cs', 'InstallEngine.cs', 'SetupForm.cs', 'SetupPreview.cs', 'AssemblyInfo.cs',
  'portable/PortableGame.cs', 'portable/ImportForm.cs', 'portable/LauncherArt.cs', 'portable/Updater.cs', 'portable/Program.cs') |
  ForEach-Object { Join-Path $root "setup/$_" }
$icon = Join-Path $root 'packaging\resources\outrun.ico'
$launcher = Join-Path $stage 'OutRun Online Arcade.exe'
& $compiler /nologo /target:winexe /platform:x64 /optimize+ /debug- /main:Outrun.Portable.Program "/out:$launcher" "/win32icon:$icon" `
  "/win32manifest:$root\setup\app.manifest" /reference:System.Core.dll /reference:System.Drawing.dll /reference:System.Windows.Forms.dll `
  /reference:System.IO.Compression.dll /reference:System.IO.Compression.FileSystem.dll /reference:System.Web.Extensions.dll `
  /reference:System.Net.Http.dll @sources
if ($LASTEXITCODE -ne 0) { throw 'launcher compile failed' }

# No build-machine folders in the binaries.
$binaries = Get-ChildItem $stage -Recurse -File -Include *.exe, *.dll | ForEach-Object { $_.FullName }
& python -I (Join-Path $root 'tools\Scrub-BuildPaths.py') @binaries
if ($LASTEXITCODE -ne 0) { throw 'a build path is left in a binary' }
# No IP addresses in the runtime's multiplayer log lines (players post their logs).
& python -I (Join-Path $root 'tools\Redact-NetplayLog.py') (Join-Path $res 'rexruntime.dll')
if ($LASTEXITCODE -ne 0) { throw 'netplay log strings not found in rexruntime.dll' }

$hashes = [ordered]@{}
foreach ($n in 'outrun.exe', 'rexruntime.dll', 'rexgpu-xenos.dll', 'TracyClient.dll', 'dxcompiler.dll', 'dxil.dll', 'native_pipelines.bin') {
  $hashes[$n] = (Get-FileHash (Join-Path $res $n)).Hash
}
$manifest = [ordered]@{ schema = 1; product = 'OutRun Online Arcade'; version = $version; layout = 'portable-resources';
  titleId = '58410968'; supportedRevision = '0.0.1.5'; supportedXexSha256 = '1E97C08AF72DD19F674A03DA1B7DC15C8825EC2283F985D71DCC92B97B6775F0';
  originalContentIncluded = $false; runtimeFiles = $hashes }
$manifest | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $res 'release-manifest.json') -Encoding UTF8

# Audit: no game files, player data or personal paths.
$files = @(Get-ChildItem $stage -Recurse -File | ForEach-Object { $_.FullName.Substring($stage.Length + 1).Replace('\', '/') }) | Sort-Object
$bad = $files | Where-Object { $_ -match '(?i)assets/|userdata|\.xex$|cache/|logs/|outrun\.toml$|pc-settings|multiplayer-settings|import-manifest|\.pdb$' }
if ($bad) { throw "forbidden in release: $bad" }
foreach ($f in $files) {
  $bytes = [IO.File]::ReadAllBytes((Join-Path $stage $f))
  $text = [Text.Encoding]::GetEncoding(28591).GetString($bytes)
  if ($text -match '(?i)[a-z]:[\\/]Users[\\/]|OneDrive') { throw "user path in $f" }
}
$sums = foreach ($n in $files) { (Get-FileHash (Join-Path $stage $n)).Hash + '  ' + $n }
[IO.File]::WriteAllLines((Join-Path $res 'SHA256SUMS.txt'), $sums, [Text.UTF8Encoding]::new($false))

Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = Join-Path $out "Outrun-Arcade-Online-XBLA-PC-Recomp-v$version.zip"
if (Test-Path $zip) { Remove-Item $zip }
$a = [IO.Compression.ZipFile]::Open($zip, 'Create')
try {
  foreach ($n in ($files + 'resources/SHA256SUMS.txt')) {
    [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($a, (Join-Path $stage $n), 'OutRun Online Arcade/' + $n, 'Optimal') | Out-Null
  }
} finally { $a.Dispose() }
$h = (Get-FileHash $zip).Hash
"$h  $(Split-Path $zip -Leaf)" | Set-Content "$zip.sha256" -Encoding ascii
"ZIP: $zip"
"SHA256: $h"
"Size MB: {0:N1}" -f ((Get-Item $zip).Length / 1MB)
