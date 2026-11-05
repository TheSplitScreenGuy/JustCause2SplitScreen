# Closes the game, builds, installs, loads the latest autosave and joins Player 2.
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
python "$root\tools\jc2ctl.py" close
Set-Location $root
$b = & .\build.bat | Select-String -Pattern ": error|Built"
$b | Select-Object -First 15
if (-not ($b -match 'Built')) { exit 1 }
& .\install.ps1 | Out-Null
python "$root\tools\jc2ctl.py" loadsave 22
python "$root\tools\jc2ctl.py" mod cam | Select-Object -First 1
python "$root\tools\jc2ctl.py" mod p2 join
Start-Sleep 12
