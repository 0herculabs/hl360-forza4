param(
    [Parameter(Mandatory=$true)][string]$GameDir,
    [Parameter(Mandatory=$true)][string]$UserDir
)
$ErrorActionPreference = 'Stop'
function AssertNoLinks([string]$path) {
    $probe = [IO.Path]::GetFullPath($path)
    while ($probe) {
        if ((Test-Path -LiteralPath $probe) -and ((Get-Item -LiteralPath $probe -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "Linked path: $probe" }
        $parent = Split-Path $probe -Parent
        if ($parent -eq $probe) { break }
        $probe = $parent
    }
}
function AssertTree([string]$path) {
    AssertNoLinks $path
    if (!(Test-Path -LiteralPath $path)) { return }
    foreach ($item in Get-ChildItem -LiteralPath $path -Force) {
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Linked mod entry: $($item.FullName)" }
        if ($item.PSIsContainer) { AssertTree $item.FullName }
    }
}
try {
    $GameDir = [IO.Path]::GetFullPath($GameDir).TrimEnd('\')
    $UserDir = [IO.Path]::GetFullPath($UserDir).TrimEnd('\')
    if ($GameDir -eq $UserDir) { throw 'Game and user roots must be separate' }
    $modGame = [IO.Path]::GetFullPath((Join-Path $GameDir '.fm4-plus'))
    $modUser = [IO.Path]::GetFullPath((Join-Path $UserDir '.fm4-plus'))
    # Check absolute deletion targets before any recursive operation.
    if ((Split-Path $modGame -Parent) -ne $GameDir -or (Split-Path $modUser -Parent) -ne $UserDir) { throw 'Invalid mod roots' }
    if ($modGame.StartsWith($modUser+'\',[StringComparison]::OrdinalIgnoreCase) -or
        $modUser.StartsWith($modGame+'\',[StringComparison]::OrdinalIgnoreCase) -or
        $GameDir.StartsWith($modUser+'\',[StringComparison]::OrdinalIgnoreCase) -or
        $UserDir.StartsWith($modGame+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Overlapping mod roots' }
    if (!(Test-Path -LiteralPath (Join-Path $GameDir 'default.xex')) -or
        !(Test-Path -LiteralPath (Join-Path $modGame '.fm4-plus-ready')) -or
        !(Test-Path -LiteralPath (Join-Path $modUser '.fm4-plus-ready'))) { throw 'A complete separate Forza Plus installation is required' }
    AssertTree $modGame
    AssertTree $modUser
    $content = [IO.Path]::GetFullPath((Join-Path $modUser '0000000000000000/4D530910'))
    if (!$content.StartsWith($modUser+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Content escaped mod user root' }
    # Preserve profile folders, settings and caches in-place. A reinstall may
    # reuse this user root only when this explicit marker exists.
    [IO.File]::WriteAllText((Join-Path $modUser '.fm4-plus-preserved'), '1')
    if (Test-Path -LiteralPath $content) { Remove-Item -LiteralPath $content -Recurse -Force }
    Remove-Item -LiteralPath $modGame -Recurse -Force
    foreach ($name in @('.fm4-plus-ready','.fm4-plus.sha256')) {
        $file = Join-Path $modUser $name
        if (Test-Path -LiteralPath $file) { Remove-Item -LiteralPath $file -Force }
    }
    Write-Output 'Forza Plus uninstalled. Original game, mod saves and settings preserved. Install again to select another career mode.'
} catch {
    Write-Output "Forza Plus uninstall: $($_.Exception.Message)"
    exit 1
}
