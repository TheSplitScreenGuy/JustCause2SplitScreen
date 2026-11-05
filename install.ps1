# Copies the built mod into the Just Cause 2 folder. Undo with uninstall.ps1.
# The game folder is found through Steam; pass -GameDir (or set JC2_DIR) to pick it yourself.
param([string]$GameDir = "")
if (-not $GameDir) { $GameDir = & (Join-Path $PSScriptRoot "tools\FindGame.ps1") }

$dll = Join-Path $PSScriptRoot "build\dinput8.dll"
if (-not (Test-Path $dll)) { throw "Build first: $dll not found" }
if (-not (Test-Path (Join-Path $GameDir "JustCause2.exe"))) { throw "JustCause2.exe not found in $GameDir" }

Copy-Item $dll $GameDir -Force
Copy-Item (Join-Path $PSScriptRoot "build\dinput8.pdb") $GameDir -Force -ErrorAction SilentlyContinue
Write-Host "Installed to $GameDir. Log file: $GameDir\jc2ss.log"
