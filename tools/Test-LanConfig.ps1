[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$test=Join-Path $root ('out/lan-config-test-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $test | Out-Null
$compiler=Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
$sources=@('LanConfig.cs','LanConfigTests.cs','SetupPreview.cs') | ForEach-Object {Join-Path $root "setup/$_"}
& $compiler /nologo /target:exe /platform:x64 /main:Outrun.Setup.LanConfigTests "/out:$test/LanConfigTests.exe" /reference:System.Core.dll /reference:System.Drawing.dll /reference:System.Windows.Forms.dll @sources
if($LASTEXITCODE -ne 0){throw 'LAN configuration tests did not compile.'}
& "$test/LanConfigTests.exe" "$test/data"
if($LASTEXITCODE -ne 0){throw 'LAN configuration tests failed.'}
