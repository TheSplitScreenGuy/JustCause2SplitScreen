# Returns the Just Cause 2 folder: $env:JC2_DIR if set, otherwise the Steam library that has it.
#   $game = & "$PSScriptRoot\tools\FindGame.ps1"
if ($env:JC2_DIR) { return $env:JC2_DIR }
$steam = (Get-ItemProperty "HKCU:\Software\Valve\Steam" -ErrorAction SilentlyContinue).SteamPath
if (-not $steam) { throw "Steam not found; set JC2_DIR to the Just Cause 2 folder" }
$libraries = @($steam)
$vdf = Join-Path $steam "steamapps\libraryfolders.vdf"
if (Test-Path $vdf) {
    $libraries += [regex]::Matches((Get-Content $vdf -Raw), '"path"\s+"([^"]+)"') | ForEach-Object { $_.Groups[1].Value -replace '\\\\', '\' }
}
foreach ($lib in $libraries) {
    # .NET paths, not Join-Path: a library on a drive that isn't plugged in must not throw.
    $dir = [IO.Path]::Combine($lib, "steamapps\common\Just Cause 2")
    if ([IO.File]::Exists([IO.Path]::Combine($dir, "JustCause2.exe"))) { return [IO.Path]::GetFullPath($dir) }
}
throw "Just Cause 2 not found in Steam's libraries; set JC2_DIR to its folder"
