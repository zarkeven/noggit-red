# Sync dist/definitions/*.dbd from upstream wowdev/WoWDBDefs.
# Usage:
#   powershell -File tools/sync_wowdbdefs.ps1
#   powershell -File tools/sync_wowdbdefs.ps1 -CacheDir D:\cache\WoWDBDefs
#
# Does not commit. Leaves dist/definitions ready for review / submodule commit.

[CmdletBinding()]
param(
  [string]$RepoRoot = "",
  [string]$CacheDir = (Join-Path $env:TEMP "noggit-WoWDBDefs"),
  [string]$RemoteUrl = "https://github.com/wowdev/WoWDBDefs.git",
  [string]$Branch = "master"
)

$ErrorActionPreference = "Stop"

if (-not $RepoRoot) {
  $scriptDir = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
  if (-not $scriptDir) { $scriptDir = (Get-Location).Path }
  $RepoRoot = (Resolve-Path (Join-Path $scriptDir "..")).Path
}

$dest = Join-Path $RepoRoot "dist\definitions"
if (-not (Test-Path $dest)) {
  throw "Missing destination directory: $dest"
}

Write-Host "Cache: $CacheDir"
Write-Host "Dest:  $dest"

if (-not (Test-Path (Join-Path $CacheDir ".git"))) {
  New-Item -ItemType Directory -Force -Path $CacheDir | Out-Null
  if (Test-Path $CacheDir) {
    # Empty or non-git dir: clone fresh into cache.
    $existing = Get-ChildItem $CacheDir -Force -ErrorAction SilentlyContinue
    if ($existing) {
      Remove-Item -Recurse -Force $CacheDir
      New-Item -ItemType Directory -Force -Path $CacheDir | Out-Null
    }
  }
  git clone --depth 1 --branch $Branch $RemoteUrl $CacheDir
  if ($LASTEXITCODE -ne 0) { throw "git clone failed" }
} else {
  Push-Location $CacheDir
  try {
    git fetch --depth 1 origin $Branch
    if ($LASTEXITCODE -ne 0) { throw "git fetch failed" }
    git checkout $Branch
    git reset --hard "origin/$Branch"
    if ($LASTEXITCODE -ne 0) { throw "git reset failed" }
  } finally {
    Pop-Location
  }
}

$srcDefs = Join-Path $CacheDir "definitions"
if (-not (Test-Path $srcDefs)) {
  throw "WoWDBDefs has no definitions/ directory at $srcDefs"
}

$srcFiles = Get-ChildItem $srcDefs -Filter "*.dbd" -File
if ($srcFiles.Count -eq 0) {
  throw "No .dbd files found in $srcDefs"
}

Write-Host "Copying $($srcFiles.Count) .dbd files..."
$copied = 0
foreach ($f in $srcFiles) {
  Copy-Item -Force -Path $f.FullName -Destination (Join-Path $dest $f.Name)
  $copied++
}

# Report a few SL pins we care about.
$checks = @("Light.dbd", "LightData.dbd", "Map.dbd", "MapDifficulty.dbd")
foreach ($name in $checks) {
  $path = Join-Path $dest $name
  if (-not (Test-Path $path)) {
    Write-Warning "Missing after sync: $name"
    continue
  }
  $hits = Select-String -Path $path -Pattern "9\.2\.7\.45745" -SimpleMatch:$false | Measure-Object
  Write-Host ("  {0}: 9.2.7.45745 matches = {1}" -f $name, $hits.Count)
}

Write-Host "Done. Copied $copied DBD files into $dest"
Write-Host "Tip: commit inside the dist/definitions submodule if you publish that repo separately."
