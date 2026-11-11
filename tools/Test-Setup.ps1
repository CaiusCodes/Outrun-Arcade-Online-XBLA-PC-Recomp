[CmdletBinding()]
param([string]$SetupExecutable, [string]$InstalledDirectory)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$projectRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$testRoot=Join-Path $projectRoot ('out\tests\'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $testRoot -Force | Out-Null
$compiler=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
$testExe=Join-Path $testRoot 'ReaderTests.exe'
& $compiler /nologo /target:exe /platform:x64 "/out:$testExe" /reference:System.Core.dll (Join-Path $projectRoot 'setup\StfsPackage.cs') (Join-Path $projectRoot 'setup\ReaderTests.cs')
if($LASTEXITCODE -ne 0){throw 'Reader tests did not compile.'}
& $testExe $testRoot
if($LASTEXITCODE -ne 0){throw 'Reader tests failed.'}
if ($InstalledDirectory) {
    $source=Join-Path $projectRoot 'private\assets'
    $installedAssets=Join-Path $InstalledDirectory 'Game\assets'
    $originalFiles=@(Get-ChildItem -LiteralPath $source -Recurse -File)
    if(@(Get-ChildItem -LiteralPath $installedAssets -Recurse -File).Count -ne $originalFiles.Count){throw 'Asset count mismatch.'}
    foreach($file in $originalFiles){
        $relative=$file.FullName.Substring($source.Length+1)
        $other=Join-Path $installedAssets $relative
        if((Get-FileHash -LiteralPath $file.FullName).Hash -ne (Get-FileHash -LiteralPath $other).Hash){throw "Asset mismatch: $relative"}
    }
    Write-Output "PASS: $($originalFiles.Count) installed assets match the development import byte for byte."
}
if ($SetupExecutable) {
    $isolatedSetup=Join-Path $testRoot 'Setup under test.exe'
    Copy-Item -LiteralPath $SetupExecutable -Destination $isolatedSetup
    $SetupExecutable=$isolatedSetup
    $target=Join-Path $testRoot 'must-not-install'
    $badPackage=Join-Path $testRoot 'wrong-title.stfs'
    $process=Start-Process -FilePath $SetupExecutable -ArgumentList @('--install',('"'+$badPackage+'"'),('"'+$target+'"')) -WindowStyle Hidden -Wait -PassThru
    if($process.ExitCode -ne 1 -or (Test-Path -LiteralPath $target)){throw 'Wrong-title installation was not rejected.'}
    Write-Output 'PASS: setup rejects the wrong game before creating the destination.'
    if($InstalledDirectory){
        $before=(Get-FileHash -LiteralPath (Join-Path $InstalledDirectory 'Game\assets\default.xex')).Hash
        $package=(Get-ChildItem (Join-Path $projectRoot 'ROM') -Recurse -File | Select-Object -First 1).FullName
        $process=Start-Process -FilePath $SetupExecutable -ArgumentList @('--install',('"'+$package+'"'),('"'+$InstalledDirectory+'"')) -WindowStyle Hidden -Wait -PassThru
        if($process.ExitCode -ne 1 -or $before -ne (Get-FileHash -LiteralPath (Join-Path $InstalledDirectory 'Game\assets\default.xex')).Hash){throw 'Existing installation protection failed.'}
        Write-Output 'PASS: setup refuses to overwrite an existing installation.'
    }
}
