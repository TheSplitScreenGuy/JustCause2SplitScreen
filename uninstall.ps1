# Removes the mod from the Just Cause 2 folder.
# The game folder is found through Steam; pass -GameDir (or set JC2_DIR) to pick it yourself.
param([string]$GameDir = "")
if (-not $GameDir) { $GameDir = & (Join-Path $PSScriptRoot "tools\FindGame.ps1") }

foreach ($name in "dinput8.dll", "dinput8.pdb", "jc2ss.log") {
    $path = Join-Path $GameDir $name
    if (Test-Path $path) { Remove-Item $path -Force; Write-Host "Removed $path" }
}
