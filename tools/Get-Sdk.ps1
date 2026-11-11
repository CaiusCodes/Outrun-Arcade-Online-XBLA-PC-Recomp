[CmdletBinding()]
param([string]$Destination,[string]$LocalMirror,[switch]$SkipPatches,[switch]$Vulkan=$true)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
if(-not $Destination){$Destination=Join-Path $root 'out/dependencies/rexglue-sdk'}
$Destination=[IO.Path]::GetFullPath($Destination)
$pin='3eb9b511b4140d2769e27be63eae57d41bfa2afa'
function GitRun([string[]]$Arguments){ & git @Arguments;if($LASTEXITCODE -ne 0){throw "Git failed: $($Arguments[0])"} }
if(-not (Test-Path -LiteralPath (Join-Path $Destination '.git'))) {
 if(Test-Path -LiteralPath $Destination){throw 'Choose a new empty SDK destination; existing files are never replaced.'}
 $origin='https://github.com/rexglue/rexglue-sdk.git'
 $cloneSource=$origin;if($LocalMirror){$cloneSource=[IO.Path]::GetFullPath($LocalMirror)}
 GitRun @('clone','--no-hardlinks','--no-checkout',$cloneSource,$Destination)
 GitRun @('-C',$Destination,'remote','set-url','origin',$origin)
 GitRun @('-C',$Destination,'checkout','--detach',$pin)
}
$head=& git -C $Destination rev-parse HEAD
if($head -ne $pin){throw 'SDK checkout does not match the pinned commit.'}
# Only the dependencies used by the Windows D3D12/tool build are fetched.
$modules=@('cli11','libmspack','FFmpeg','tomlplusplus','simde','xxHash','spdlog','fmt','snappy','utfcpp','imgui','sdl3','tracy','o1heap','inja')
if($Vulkan){$modules+=@('volk','vulkan-headers','vulkan-memory-allocator','spirv-headers','spirv-tools','glslang')}
foreach($module in $modules) {
 $relative='thirdparty/'+$module
 $expected=(& git -C $Destination ls-tree HEAD -- $relative).Split()[2]
 if($LocalMirror -and -not (Test-Path -LiteralPath (Join-Path $Destination "$relative/.git"))) {
  GitRun @('clone','--no-hardlinks','--no-checkout',(Join-Path $LocalMirror $relative),(Join-Path $Destination $relative))
  GitRun @('-C',(Join-Path $Destination $relative),'checkout','--detach',$expected)
  $url= & git -C $Destination config -f .gitmodules --get "submodule.$relative.url"
  GitRun @('-C',(Join-Path $Destination $relative),'remote','set-url','origin',$url)
 } elseif(-not (Test-Path -LiteralPath (Join-Path $Destination "$relative/.git"))) {
  GitRun @('-C',$Destination,'submodule','update','--init','--',$relative)
 }
 $actual=& git -C (Join-Path $Destination $relative) rev-parse HEAD
 if($actual -ne $expected){throw "Dependency pin mismatch: $module"}
}
if(-not $SkipPatches){
 & python (Join-Path $root 'tools/Apply-SdkPatches.py') $Destination
 if($LASTEXITCODE -ne 0){throw 'SDK patch validation failed; existing source edits were preserved.'}
 # Windows may check cabextract/mspack symlinks out as path-only text files.
 # Validate both the CMake selection and its real source/header files.
 $cmake=Join-Path $Destination 'thirdparty/CMakeLists.txt'
 $selection='set(MSPACK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libmspack/libmspack/mspack")'
 if(-not ([IO.File]::ReadAllText($cmake).Contains($selection))){
  throw 'SDK must build libmspack from libmspack/libmspack/mspack, not cabextract symlink stubs.'
 }
 foreach($file in @('lzxd.c','lzx.h','system.h','mspack.h')){
  $source=Join-Path $Destination "thirdparty/libmspack/libmspack/mspack/$file"
  if(-not (Test-Path -LiteralPath $source -PathType Leaf) -or (Get-Item -LiteralPath $source).Length -lt 128){
   throw "Missing real libmspack source/header: $source"
  }
 }
}
Write-Host "Pinned SDK ready: $Destination"
