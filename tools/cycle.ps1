# Build the mod, restart the game with it and load the latest autosave.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$out = cmd /c "`"$root\build.bat`""
$out | Select-String -Pattern 'error|warning|Built' | ForEach-Object { $_.Line }
if ($LASTEXITCODE -ne 0) { throw 'build failed' }
python -I "$root\tools\jc2ctl.py" close
& powershell -ExecutionPolicy Bypass -File "$root\install.ps1"
python -I "$root\tools\jc2ctl.py" loadsave
python -I "$root\tools\jc2ctl.py" focus
# Test runs teleport the players around; keep them from dying on landing ("god off" to undo).
python -I "$root\tools\jc2ctl.py" mod "god on" | Out-Null
