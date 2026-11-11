[CmdletBinding()]
param([Parameter(Mandatory)][string]$ImportedDirectory)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$test=Join-Path $root ('out/portable-ui-tests/'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $test | Out-Null
$compiler=Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
$sources=@('StfsPackage.cs','GameValidation.cs','InstallEngine.cs','SetupForm.cs','portable/PortableGame.cs','portable/ImportForm.cs','portable/LauncherArt.cs','portable/PortableUiTests.cs')|ForEach-Object{Join-Path $root "setup/$_"}
& $compiler /nologo /target:exe /platform:x64 /main:Outrun.Setup.PortableUiTests "/out:$test/PortableUiTests.exe" /reference:System.Core.dll /reference:System.Drawing.dll /reference:System.Windows.Forms.dll /reference:System.IO.Compression.dll /reference:System.IO.Compression.FileSystem.dll /reference:System.Web.Extensions.dll @sources
if($LASTEXITCODE -ne 0){throw 'UI tests compilation failed.'}
& "$test/PortableUiTests.exe" "$test/fresh" ([IO.Path]::GetFullPath($ImportedDirectory))
if($LASTEXITCODE -ne 0){throw "UI checks failed; evidence $test"}
Write-Output "Evidence: $test"
