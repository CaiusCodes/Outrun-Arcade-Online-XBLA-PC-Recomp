[CmdletBinding()]
param([string]$Destination = 'out/multiplayer-local-20260920')
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$source=Join-Path $root 'out/checkpoints/0.1.5-dev-20260920/Game'
$dest=[IO.Path]::GetFullPath((Join-Path $root $Destination))
$outRoot=[IO.Path]::GetFullPath((Join-Path $root 'out'))+[IO.Path]::DirectorySeparatorChar
if(-not $dest.StartsWith($outRoot,[StringComparison]::OrdinalIgnoreCase)){throw 'Test copies must stay under project out/.'}
if(Test-Path -LiteralPath $dest){throw 'Test directory exists; preserve its data and choose a new destination.'}
New-Item -ItemType Directory -Path $dest | Out-Null
foreach($role in @('Host','Join')){
    $target=Join-Path $dest $role
    New-Item -ItemType Directory -Path $target | Out-Null
    foreach($name in @('outrun.exe','rexruntime.dll','rexgpu-xenos.dll','TracyClient.dll','assets','userdata','licenses')){
        Copy-Item -LiteralPath (Join-Path $source $name) -Destination $target -Recurse
    }
    $address=if($role -eq 'Host'){'127.0.0.1'}else{'127.0.0.2'}
    # Direct target prevents broadcast discovery during this same-PC experiment.
    $config=[IO.File]::ReadAllText((Join-Path $source 'outrun.toml'))
    $config+="`n# Isolated loopback multiplayer test; no relay.`nnet_private_match = true`nnet_address = `"$address`"`nnet_join_address = `"127.0.0.1`"`nnet_player_name = `"OutRun $role`"`nnet_relay_server = `"`"`nlog_level = `"debug`"`n"
    [IO.File]::WriteAllText((Join-Path $target 'outrun.toml'),$config)
    # Two 720p windows reduce combined GPU load and leave room for both clients.
    [IO.File]::WriteAllText((Join-Path $target 'pc-settings.toml'),"fullscreen = false`nresolution_scale = 1`nshow_fps = true`n")
    [IO.File]::WriteAllText((Join-Path $dest ("Start "+$role+'.bat')),"@echo off`r`ncd /d `"%~dp0$role`"`r`nstart `"`" outrun.exe`r`n")
}
Write-Output "Created isolated Host/Join copies: $dest"
