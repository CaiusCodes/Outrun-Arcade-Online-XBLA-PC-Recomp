[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$projectRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$testRoot=Join-Path $projectRoot ('out\regression\'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $testRoot -Force | Out-Null
$payload=(Get-ChildItem (Join-Path $projectRoot 'out\setup-build') -Filter payload.zip -Recurse | Sort-Object LastWriteTime -Descending | Select-Object -First 1).FullName
$compiler=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
$testExe=Join-Path $testRoot 'InstallRegressionTests.exe'
$sources=@('StfsPackage.cs','GameValidation.cs','InstallEngine.cs','InstallRegressionTests.cs') | ForEach-Object {Join-Path $projectRoot "setup\$_"}
& $compiler /nologo /target:exe /platform:x64 "/out:$testExe" /reference:System.Core.dll /reference:System.IO.Compression.dll "/resource:$payload,Outrun.Payload" @sources
if($LASTEXITCODE -ne 0){throw 'Regression test compilation failed.'}
$package=Get-ChildItem (Join-Path $projectRoot 'ROM') -Recurse -File | Select-Object -First 1 -ExpandProperty FullName
& $testExe $package $testRoot
if($LASTEXITCODE -ne 0){throw 'Install regression failed.'}
Write-Output "Regression files: $testRoot"
