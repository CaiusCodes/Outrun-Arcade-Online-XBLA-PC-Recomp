[CmdletBinding()]
param([Parameter(Mandatory)][string]$PortableDirectory,[string]$OriginalPackage)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
if(-not $OriginalPackage){$OriginalPackage=(Get-ChildItem (Join-Path $root 'ROM') -Recurse -File|Select-Object -First 1).FullName}
if(-not $OriginalPackage -or -not (Test-Path -LiteralPath $OriginalPackage -PathType Leaf)){throw 'The original package test fixture is missing. Supply -OriginalPackage to repeat fresh-import checks.'}
$test=Join-Path $root ('out/portable-tests/'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $test | Out-Null
$compiler=Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
$sources=@('StfsPackage.cs','GameValidation.cs','InstallEngine.cs','SetupForm.cs','SetupPreview.cs','LanConfig.cs','portable/PortableGame.cs','portable/ImportForm.cs','portable/LauncherArt.cs','portable/PortableTests.cs')|ForEach-Object{Join-Path $root "setup/$_"}
& $compiler /nologo /target:exe /platform:x64 /define:OUTRUN_PORTABLE_TESTS /main:Outrun.Setup.PortableTests "/out:$test/PortableTests.exe" /reference:System.Core.dll /reference:System.Drawing.dll /reference:System.Windows.Forms.dll /reference:System.IO.Compression.dll /reference:System.IO.Compression.FileSystem.dll /reference:System.Web.Extensions.dll @sources
if($LASTEXITCODE -ne 0){throw 'Portable tests compilation failed.'}
$package=[IO.Path]::GetFullPath($OriginalPackage)
& "$test/PortableTests.exe" "$test/data" $package ([IO.Path]::GetFullPath($PortableDirectory)) (Join-Path $root 'private/assets')
if($LASTEXITCODE -ne 0){throw "Portable checks failed; evidence $test"}
Write-Output "Evidence: $test"
