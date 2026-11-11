[CmdletBinding()]
param([ValidateRange(1,32)][int]$Jobs=6)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$sdk=Join-Path $root 'out/dependencies/rexglue-sdk'
$install=Join-Path $root 'out/sdk-install'
& (Join-Path $PSScriptRoot 'Get-Sdk.ps1') -Destination $sdk
& (Join-Path $PSScriptRoot 'Build-Sdk.ps1') -SdkDirectory $sdk -InstallDirectory $install -Jobs $Jobs
Write-Host "Outrun SDK ready: $install"
