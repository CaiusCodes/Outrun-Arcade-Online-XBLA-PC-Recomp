[CmdletBinding()]
param([string]$Package, [string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $Package) {
    $packages = @(Get-ChildItem -LiteralPath (Join-Path $projectRoot 'ROM') -Recurse -File)
    if ($packages.Count -ne 1) { throw 'Specify -Package with the path to your OutRun Online Arcade Xbox 360 package.' }
    $Package = $packages[0].FullName
}
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $projectRoot 'private\assets' }
$toolRoot = Join-Path $projectRoot 'out\tools'
New-Item -ItemType Directory -Path $toolRoot -Force | Out-Null
$compiler = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
$importer = Join-Path $toolRoot 'ImportOutrun.exe'
& $compiler /nologo /target:exe /platform:x64 /optimize+ "/out:$importer" /reference:System.Core.dll (Join-Path $projectRoot 'setup\StfsPackage.cs') (Join-Path $projectRoot 'setup\GameValidation.cs') (Join-Path $projectRoot 'setup\ImportCli.cs')
if ($LASTEXITCODE -ne 0) { throw 'Importer compilation failed.' }
& $importer $Package $OutputDirectory
if ($LASTEXITCODE -ne 0) { throw 'Import failed. Any partial output is preserved for inspection; retry into a new folder.' }
$receipt = [ordered]@{
    title = 'OutRun Online Arcade'; title_id = '58410968'
    package_sha256 = (Get-FileHash -LiteralPath $Package -Algorithm SHA256).Hash
    xex_sha256 = (Get-FileHash -LiteralPath (Join-Path $OutputDirectory 'default.xex') -Algorithm SHA256).Hash
    file_count = @(Get-ChildItem -LiteralPath $OutputDirectory -Recurse -File).Count
    imported_utc = [DateTime]::UtcNow.ToString('o')
}
$receipt | ConvertTo-Json | Set-Content -LiteralPath (Join-Path (Split-Path $OutputDirectory -Parent) 'import-receipt.json') -Encoding UTF8
