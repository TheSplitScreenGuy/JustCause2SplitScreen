# Waypoint test with Player 2 joined and the given [Debug] keys in jc2ss.ini (restarts the game):
#   powershell -File tools\tests\waypointp2.ps1 "SkipP2ActionInput,SkipP2Brain"
param([string]$KeyList = "")
$Keys = @($KeyList -split ',' | Where-Object { $_ })
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$ini = Join-Path (& "$root\tools\FindGame.ps1") "jc2ss.ini"
if ($Keys) { ("[Debug]`r`n" + (($Keys | ForEach-Object { "$_=1" }) -join "`r`n") + "`r`n") | Out-File -Encoding ascii $ini }
elseif (Test-Path $ini) { Remove-Item $ini }
python -I "$root\tools\jc2ctl.py" close
python -I "$root\tools\jc2ctl.py" loadsave | Out-Null
python -I "$root\tools\jc2ctl.py" mod "p2 join" | Out-Null
Start-Sleep 8
"keys: $($Keys -join ' ')"
python -I "$root\tools\tests\waypoint.py"
