# Packages a release build into out/releases/FM4-<version>-windows-x64.zip.
# Usage: powershell -ExecutionPolicy Bypass -File scripts\package-release.ps1 -Version v0.4.0
param(
    [Parameter(Mandatory = $true)][string]$Version,
    [string]$BuildDir = "out\build\win-amd64-release",
    [string]$SdkSource = "thirdparty\shiftglue-src"
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$notes = "doc\releases\$Version.md"
if (-not (Test-Path $notes)) { throw "Missing release notes: $notes" }
$build = Join-Path $root $BuildDir
foreach ($exe in "fm4.exe", "fm4-launcher.exe", "fm4tool.exe", "fm4-wheel-probe.exe") {
    if (-not (Test-Path (Join-Path $build $exe))) { throw "Missing $exe in $BuildDir; build release first" }
}

$stage = Join-Path $root "out\releases\$Version-release"
$dest = Join-Path $stage "FM4"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force $dest, "$dest\doc\img", "$dest\doc\releases", "$dest\licenses\SDL3" | Out-Null

# Binaries, runtime DLLs and the files the launcher reads next to itself.
Get-ChildItem $build -File | Where-Object {
    $_.Extension -in ".exe", ".dll" -and $_.Name -notlike "*-tests.exe"
} | Copy-Item -Destination $dest
foreach ($f in "fm4.toml", "game_files.sha256", "install-forza-plus.ps1", "uninstall-forza-plus.ps1") {
    Copy-Item (Join-Path $build $f) $dest
}
Set-Content -Encoding ascii "$dest\launch.cmd" "@echo off`r`ncd /d `"%~dp0`"`r`nstart `"`" `"%~dp0fm4-launcher.exe`""

# Documentation.
Copy-Item README.md $dest
Copy-Item $notes "$dest\RELEASE-NOTES.md"
Copy-Item $notes "$dest\doc\releases\"
Get-ChildItem doc -File -Filter *.md | Copy-Item -Destination "$dest\doc"
Get-ChildItem doc\img -File | Copy-Item -Destination "$dest\doc\img"

# Third-party licences.
$sdkName = if ($SdkSource -match "shiftglue") { "ShiftGlue-SDK.txt" } else { "ReXGlue-SDK.txt" }
Copy-Item (Join-Path $SdkSource "LICENSE") "$dest\licenses\$sdkName"
Copy-Item (Join-Path $SdkSource "thirdparty\sdl3\LICENSE.txt") "$dest\licenses\SDL3\LICENSE.txt"

# Checksums of every packaged file, then the archive and its own checksum.
Push-Location $dest
Get-ChildItem -Recurse -File | Where-Object Name -ne "SHA256SUMS.txt" | Sort-Object FullName | ForEach-Object {
    $rel = $_.FullName.Substring($dest.Length + 1).Replace("\", "/")
    "{0}  {1}" -f (Get-FileHash -Algorithm SHA256 $_.FullName).Hash.ToLower(), $rel
} | Set-Content -Encoding utf8 SHA256SUMS.txt
Pop-Location

$zip = Join-Path $root "out\releases\FM4-$Version-windows-x64.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path $dest -DestinationPath $zip -CompressionLevel Optimal
$hash = (Get-FileHash -Algorithm SHA256 $zip).Hash.ToLower()
Set-Content -Encoding ascii "$zip.sha256" "$hash  $(Split-Path -Leaf $zip)"
Write-Host "Packaged $zip"
Write-Host "SHA-256 $hash"
