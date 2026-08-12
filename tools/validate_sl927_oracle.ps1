# Helper notes + env probe for SL 9.2.7 / wowlib oracle validation.
# Does not require wowlib to be built — checks client path and DBD pins.

[CmdletBinding()]
param(
  [string]$ClientDir = "D:\WoWBuildArchive\9.x\9.2.7.45338",
  [string]$RepoRoot = "C:\Users\donal\Desktop\noggit-red-upstream",
  [string]$ExpectedBuild = "9.2.7.45745"
)

$ErrorActionPreference = "Stop"

Write-Host "=== SL 9.2.7 validation probe ==="
Write-Host "ClientDir: $ClientDir"
Write-Host "Expected DBD pin: $ExpectedBuild"

if (-not (Test-Path $ClientDir)) {
  Write-Warning "Client directory not found. Set -ClientDir to your 9.2.7 install."
} else {
  $bi = Join-Path $ClientDir ".build.info"
  if (Test-Path $bi) {
    Write-Host "Found .build.info"
    Select-String -Path $bi -Pattern "9\.2\.7" | Select-Object -First 3 | ForEach-Object { $_.Line }
  } else {
    Write-Warning "No .build.info under client dir"
  }
  $data = Join-Path $ClientDir "Data"
  Write-Host ("Data/ exists: {0}" -f (Test-Path $data))
}

$defs = Join-Path $RepoRoot "dist\definitions"
foreach ($t in @("Light","LightData","Map","MapDifficulty","LightParams","LightSkybox","ZoneLight","ZoneLightPoint")) {
  $p = Join-Path $defs "$t.dbd"
  if (-not (Test-Path $p)) { Write-Warning "Missing $t.dbd"; continue }
  $n = (Select-String -Path $p -Pattern ([regex]::Escape($ExpectedBuild)) | Measure-Object).Count
  Write-Host ("  {0}.dbd has {1}: {2}" -f $t, $ExpectedBuild, $(if ($n -gt 0) { "yes" } else { "NO" }))
}

$pin = Join-Path $RepoRoot "src\noggit\map_light_target.hpp"
if (Test-Path $pin) {
  Select-String -Path $pin -Pattern "45745|9\.2\.7" | ForEach-Object { "map_light_target: $($_.Line.Trim())" }
}

Write-Host ""
Write-Host "To run wowlib integration tests (gcc16), set:"
Write-Host ("  `$env:WOWLIB_TEST_CLIENTS_DIR = '{0}'" -f (Split-Path $ClientDir -Parent))
Write-Host ("  `$env:WOWLIB_TEST_LISTFILE = '{0}'" -f (Join-Path $RepoRoot "dist\listfile\listfile.csv"))
Write-Host "See docs/sl_927_client_data.md"
