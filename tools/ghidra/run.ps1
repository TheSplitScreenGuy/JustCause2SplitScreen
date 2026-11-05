# Runs a Ghidra script headless on the JC2 project (read-only).
#   powershell -File tools\ghidra\run.ps1 <Script.java> <out.txt> <args...>
# Where Ghidra and the project live comes from tools\ghidra\local.ps1 (not in the repo), e.g.:
#   $GhidraHome = "D:\Tools\ghidra_12.2_PUBLIC"; $ProjectDir = "D:\Tools\projects"; $SettingsDir = "D:\Tools\ghidra-headless"
param([Parameter(Mandatory)] [string]$Script, [Parameter(Mandatory)] [string]$Out, [Parameter(ValueFromRemainingArguments)] [string[]]$Rest)
$local = Join-Path $PSScriptRoot "local.ps1"
if (-not (Test-Path $local)) { throw "Create $local with `$GhidraHome, `$ProjectDir and `$SettingsDir (see the top of run.ps1)" }
. $local
$env:_JAVA_OPTIONS = "-Dapplication.settingsdir=$SettingsDir\settings -Dapplication.cachedir=$SettingsDir\cache"
& "$GhidraHome\support\analyzeHeadless.bat" $ProjectDir JC2 -process JustCause2.exe -noanalysis -readOnly `
    -scriptPath $PSScriptRoot -postScript $Script $Out @Rest *> "$env:TEMP\ghidra_run.txt"
if (-not (Test-Path $Out)) { Get-Content "$env:TEMP\ghidra_run.txt" -Tail 20 }
