[CmdletBinding()]
param([Parameter(Mandatory)][string]$ZipPath)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
Add-Type -AssemblyName System.IO.Compression.FileSystem
$projectRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$testRoot=Join-Path $projectRoot ('out\same-folder-test-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
[IO.Compression.ZipFile]::ExtractToDirectory([IO.Path]::GetFullPath($ZipPath),$testRoot)
$target=Join-Path $testRoot 'OutRun Online Arcade Setup'
$setupExe=Join-Path $target 'Setup OutRun Online Arcade.exe'
Set-Content -LiteralPath (Join-Path $target 'personal-notes.txt') -Value 'Keep this file.'
Set-Content -LiteralPath (Join-Path $target 'setup-error.txt') -Value 'Keep the previous diagnostic.'
$before=@(Get-ChildItem -LiteralPath $target -Recurse -File | ForEach-Object {[PSCustomObject]@{path=$_.FullName;hash=(Get-FileHash -LiteralPath $_.FullName).Hash}})
$package=(Get-ChildItem (Join-Path $projectRoot 'ROM') -Recurse -File | Select-Object -First 1).FullName
$process=Start-Process -FilePath $setupExe -WorkingDirectory $target -ArgumentList @('--install',('"'+$package+'"'),('"'+$target+'"')) -WindowStyle Hidden -Wait -PassThru
if($process.ExitCode -ne 0){throw 'Installation beside the running setup failed.'}
foreach($file in $before){if((Get-FileHash -LiteralPath $file.path).Hash -ne $file.hash){throw "Pre-existing file changed: $($file.path)"}}
if(-not (Test-Path -LiteralPath (Join-Path $target 'OutRun Online Arcade.exe')) -or -not (Test-Path -LiteralPath (Join-Path $target 'Game\installation-complete.txt'))){throw 'Missing Play launcher or completion marker.'}
Write-Output 'PASS: installs beside the running setup, preserving every original ZIP file and unrelated user file.'
$saveFolder=Join-Path $target 'Game\userdata'
New-Item -ItemType Directory -Path $saveFolder -Force | Out-Null
$saveFile=Join-Path $saveFolder 'save-preservation-test.dat'
[IO.File]::WriteAllBytes($saveFile,[byte[]]@(1,33,55,77,199))
$saveHash=(Get-FileHash -LiteralPath $saveFile).Hash
& (Join-Path $PSScriptRoot 'Test-Setup.ps1') -SetupExecutable $setupExe -InstalledDirectory $target
if((Get-FileHash -LiteralPath $saveFile).Hash -ne $saveHash){throw 'Save data changed.'}
Write-Output 'PASS: repeat installation is rejected and existing saves are preserved.'
foreach($reserved in @('Game','OutRun Online Arcade.exe','Configure LAN.exe','INSTALLATION-INCOMPLETE.txt')) {
    $conflict=Join-Path $testRoot ('conflict-'+[Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $conflict | Out-Null
    $conflictFile=Join-Path $conflict $reserved
    Set-Content -LiteralPath $conflictFile -Value 'Preserve this collision.'
    $hash=(Get-FileHash -LiteralPath $conflictFile).Hash
    $process=Start-Process -FilePath $setupExe -ArgumentList @('--install',('"'+$package+'"'),('"'+$conflict+'"')) -WindowStyle Hidden -Wait -PassThru
    if($process.ExitCode -ne 1 -or (Get-FileHash -LiteralPath $conflictFile).Hash -ne $hash){throw "Collision protection failed: $reserved"}
}
Write-Output 'PASS: existing Game, Play launcher and incomplete marker collisions are preserved.'
Write-Output "Test installation: $target"
