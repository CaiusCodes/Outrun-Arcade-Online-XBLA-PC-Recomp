[CmdletBinding()]
param([string]$Destination)
# Fetches the native renderer's libraries from upstream at pinned commits and
# applies OutRun's patches: Plume (zolaware/plume) and XenosRecomp
# (hedge-dev/XenosRecomp, with its DXC binaries and submodules).
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $Destination) { $Destination = Join-Path $root 'out\dependencies' }
New-Item -ItemType Directory -Force -Path $Destination | Out-Null
$libs = @(
  @{ Name = 'plume'; Url = 'https://github.com/zolaware/plume'; Commit = 'e0c8871b930dc1a6544bf457f2348c456b9d7e13'; Patch = 'plume.patch' },
  @{ Name = 'XenosRecomp'; Url = 'https://github.com/hedge-dev/XenosRecomp'; Commit = '990d03b28a27b50277ee5d8d942e1c5f873869d1'; Patch = 'xenosrecomp.patch' }
)
foreach ($lib in $libs) {
  $dir = Join-Path $Destination $lib.Name
  if (-not (Test-Path -LiteralPath (Join-Path $dir '.git'))) {
    & git -c core.longpaths=true clone $lib.Url $dir
    if ($LASTEXITCODE -ne 0) { throw "clone $($lib.Name) failed" }
  }
  & git -C $dir checkout -q $lib.Commit
  if ($LASTEXITCODE -ne 0) { throw "checkout $($lib.Name) $($lib.Commit) failed" }
  & git -c core.longpaths=true -C $dir submodule update --init --recursive
  if ($LASTEXITCODE -ne 0) { throw "submodules of $($lib.Name) failed" }
  $patch = Join-Path $root "patches\$($lib.Patch)"
  & git -C $dir apply --check --reverse $patch 2>$null
  if ($LASTEXITCODE -eq 0) { "$($lib.Name): patch already applied"; continue }
  & git -C $dir apply --whitespace=nowarn $patch
  if ($LASTEXITCODE -ne 0) { throw "$($lib.Patch) does not apply to $dir (reset it with git checkout -- .)" }
  "$($lib.Name): $($lib.Commit) + $($lib.Patch)"
}
