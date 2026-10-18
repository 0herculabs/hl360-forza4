param(
    [Parameter(Mandatory=$true)][string]$BaseZip,
    [string]$UpdateZip,
    [Parameter(Mandatory=$true)][string]$GameDir,
    [Parameter(Mandatory=$true)][string]$UserDir,
    [ValidateSet('Normal','Hard','Sandbox','Vanilla')][string]$Mode = 'Normal'
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$GameDir = [IO.Path]::GetFullPath($GameDir)
$UserDir = [IO.Path]::GetFullPath($UserDir)
$target = Join-Path $GameDir '.fm4-plus'
$contentTarget = Join-Path $UserDir '.fm4-plus'
$finalTarget = $target
$finalContentTarget = $contentTarget
$archives = @()
function SafePath([string]$root, [string]$relative) {
    if ($relative -match '[\\:\x00-\x1f<>"|?*]' -or $relative.StartsWith('/') -or
        @($relative.Split('/') | Where-Object { $_ -eq '' -or $_ -eq '.' -or $_ -eq '..' -or $_ -match '[. ]$' }).Count) {
        throw "Unsafe archive path: $relative"
    }
    $path = [IO.Path]::GetFullPath((Join-Path $root $relative))
    if ($path.Length -ge 260) { throw "Destination path is too long: $relative. Choose shorter game/user paths." }
    if (!$path.StartsWith($root.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Path escapes destination' }
    $probe = $path
    while ($probe -and $probe.Length -ge $root.Length) {
        if ((Test-Path -LiteralPath $probe) -and ((Get-Item -LiteralPath $probe -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "Linked destination: $probe" }
        $probe = Split-Path $probe -Parent
    }
    return $path
}
try {
    if (!(Test-Path -LiteralPath (Join-Path $GameDir 'default.xex'))) { throw 'Install Disc 1 first.' }
    $preserved = (Test-Path -LiteralPath (Join-Path $contentTarget '.fm4-plus-preserved')) -and
        !(Test-Path -LiteralPath (Join-Path $contentTarget '.fm4-plus-ready'))
    if ((Test-Path -LiteralPath $target) -or ((Test-Path -LiteralPath $contentTarget) -and !$preserved)) { throw 'Forza Plus destination already exists. Select the existing installation, or uninstall it before retrying.' }
    if ($preserved) {
        # SafePath checks links along the existing publication destinations.
        $null = SafePath $contentTarget '0000000000000000/4D530910'
        if (Test-Path -LiteralPath (Join-Path $contentTarget '0000000000000000/4D530910')) { throw 'Preserved user root still contains installed mod content' }
    }
    $stageName = '.pfp-' + [Guid]::NewGuid().ToString('N').Substring(0,8)
    $target = Join-Path $GameDir $stageName
    $contentTarget = Join-Path $UserDir $stageName
    $base = [IO.Compression.ZipFile]::OpenRead($BaseZip); $archives += $base
    $prefix = 'Project Forza Plus 4/'
    if (!$base.GetEntry($prefix + 'Main Game Changes/Media/db/gamedb.slt') -or !$base.GetEntry($prefix + 'Main Game Changes/default.xex')) { throw 'Select the full Project Forza Plus 4 ZIP, not the update.' }
    $plan = @{}
    $skipped = @{}
    function AddEntry($entry, [string]$kind, [string]$relative) {
        if ($kind -eq 'content') {
            $package = $relative.Split('/')[3]
            if ($package -notmatch '^([0-9a-fA-F]{16}|[0-9a-fA-F]{40}|[0-9a-fA-F]{42})$') { throw "Invalid content package: $relative" }
            $header = Join-Path $UserDir "0000000000000000/4D530910/Headers/00000002/$package.header"
            if (!(Test-Path -LiteralPath $header)) { $skipped[$package] = $true; return }
        }
        $root = if ($kind -eq 'game') { $target } else { $contentTarget }
        $null = SafePath $root $relative
        $plan["$kind/$relative"] = @{ Entry=$entry; Root=$root; Relative=$relative; Kind=$kind }
    }
    function AddArchive($zip, [string]$root, [bool]$update) {
        foreach ($entry in $zip.Entries) {
            if (!$entry.Name) { continue }
            $name = $entry.FullName
            $media = $root + $(if ($update) { '' } else { 'Main Game Changes/Media/' })
            if (!$update -and $name.StartsWith($media, [StringComparison]::OrdinalIgnoreCase)) {
                $rel = $name.Substring($media.Length)
                # The port owns render compatibility. Keep its cockpit shadow fix.
                if ($rel -notlike 'RenderScenarios/*') { AddEntry $entry 'game' ("Media/$rel") }
            }
            if ($update -and ($name -like "${root}audio/*" -or $name -like "${root}physics/*")) {
                AddEntry $entry 'game' ('Media/' + $name.Substring($root.Length))
            }
            $db = $root + "Game Mode DBs/$Mode Mode/"
            if ($name -eq ($db + 'gamedb.slt') -or $name -eq ($db + 'Main Game DB/gamedb.slt')) { AddEntry $entry 'game' 'Media/db/gamedb.slt' }
            $dlc = $root + $(if ($update) { "Game Mode DBs/$Mode Mode/DLC DBs/" } else { 'Disc 2 and DLCs/00000002/' })
            if ($name.StartsWith($dlc, [StringComparison]::OrdinalIgnoreCase)) {
                AddEntry $entry 'content' ('0000000000000000/4D530910/00000002/' + $name.Substring($dlc.Length))
            }
            # Mode DBs override the base package databases.
        }
        $dlcDb = $root + "Game Mode DBs/$Mode Mode/DLC DBs/"
        $modeDb = $zip.GetEntry($root + "Game Mode DBs/$Mode Mode/gamedb.slt")
        if (!$modeDb) { $modeDb = $zip.GetEntry($root + "Game Mode DBs/$Mode Mode/Main Game DB/gamedb.slt") }
        if (!$modeDb) { throw "Missing career mode: $Mode" }
        AddEntry $modeDb 'game' 'Media/db/gamedb.slt'
        foreach ($entry in $zip.Entries) {
            if ($entry.Name -and $entry.FullName.StartsWith($dlcDb, [StringComparison]::OrdinalIgnoreCase)) {
                AddEntry $entry 'content' ('0000000000000000/4D530910/00000002/' + $entry.FullName.Substring($dlcDb.Length))
            }
        }
    }
    AddArchive $base $prefix $false
    if ($UpdateZip) {
        $update = [IO.Compression.ZipFile]::OpenRead($UpdateZip); $archives += $update
        if (!$update.GetEntry('Project Forza Plus 4 Update/Update Files/Game Mode DBs/Normal Mode/gamedb.slt')) { throw 'Unrecognized Project Forza Plus 4 update ZIP.' }
        AddArchive $update 'Project Forza Plus 4 Update/Update Files/' $true
    }
    # Snapshot the source list before creating the nested destination. Never copy saves or caches.
    Write-Output 'Copying Disc 1 to a separate Forza Plus installation...'
    $sourceFiles = @(foreach ($item in Get-ChildItem -LiteralPath $GameDir -Force) {
        if ($item.Name -like '.fm4-plus*' -or $item.Name -like '.pfp-*') { continue }
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Linked source: $($item.FullName)" }
        if ($item.PSIsContainer) { Get-ChildItem -LiteralPath $item.FullName -Recurse -File -Force }
        else { $item }
    })
    $contentFiles = @()
    foreach ($sub in @('00000002','Headers/00000002')) {
        $source = Join-Path $UserDir "0000000000000000/4D530910/$sub"
        if (Test-Path -LiteralPath $source) { $contentFiles += @(Get-ChildItem -LiteralPath $source -Recurse -File) }
    }
    $gameBytes = ($sourceFiles | Measure-Object Length -Sum).Sum +
        ($plan.Values | Where-Object {$_.Kind -eq 'game'} | ForEach-Object {$_.Entry.Length} | Measure-Object -Sum).Sum + 64MB
    $contentBytes = ($contentFiles | Measure-Object Length -Sum).Sum +
        ($plan.Values | Where-Object {$_.Kind -eq 'content'} | ForEach-Object {$_.Entry.Length} | Measure-Object -Sum).Sum + 64MB
    $gameDrive = [IO.DriveInfo]::new([IO.Path]::GetPathRoot($target))
    $userDrive = [IO.DriveInfo]::new([IO.Path]::GetPathRoot($contentTarget))
    if ($gameDrive.Name -eq $userDrive.Name) { $gameBytes += $contentBytes }
    if ($gameDrive.AvailableFreeSpace -lt $gameBytes -or $userDrive.AvailableFreeSpace -lt $contentBytes) {
        throw "Not enough free disk space for the separate game and content copies."
    }
    foreach ($file in @($sourceFiles) + @($contentFiles)) {
        if ($file.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Linked source file: $($file.FullName)" }
    }
    foreach ($file in $sourceFiles) {
        $relative = $file.FullName.Substring($GameDir.TrimEnd('\').Length + 1).Replace('\','/')
        if ($relative -eq '.fm4-verified') { continue }
        $dest = SafePath $target $relative
        $null = New-Item -ItemType Directory -Path (Split-Path $dest -Parent) -Force
        Copy-Item -LiteralPath $file.FullName -Destination $dest
    }
    $title = '0000000000000000/4D530910'
    foreach ($sub in @('00000002','Headers/00000002')) {
        $source = Join-Path $UserDir "$title/$sub"
        if (Test-Path -LiteralPath $source) {
            foreach ($file in Get-ChildItem -LiteralPath $source -Recurse -File) {
                $relative = $file.FullName.Substring($UserDir.TrimEnd('\').Length+1).Replace('\','/')
                $dest = SafePath $contentTarget $relative
                $null = New-Item -ItemType Directory -Path (Split-Path $dest -Parent) -Force
                Copy-Item -LiteralPath $file.FullName -Destination $dest
            }
        }
    }
    $manifest = @()
    $contentManifest = @()
    $index=0
    foreach ($item in $plan.Values) {
        $dest = SafePath $item.Root $item.Relative
        $null = New-Item -ItemType Directory -Path (Split-Path $dest -Parent) -Force
        [IO.Compression.ZipFileExtensions]::ExtractToFile($item.Entry, $dest, $true)
        $hash = (Get-FileHash -LiteralPath $dest -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($item.Kind -eq 'game') { $manifest += "$hash $($item.Entry.Length) $($item.Relative)" }
        else { $contentManifest += "$hash $($item.Entry.Length) $($item.Relative)" }
        if ((++$index % 40) -eq 0) { Write-Output "Forza Plus: $index / $($plan.Count)" }
    }
    [IO.File]::WriteAllLines((Join-Path $target '.fm4-plus.sha256'), [string[]]$manifest)
    $null = New-Item -ItemType Directory -Path $contentTarget -Force
    [IO.File]::WriteAllLines((Join-Path $contentTarget '.fm4-plus.sha256'), [string[]]$contentManifest)
    # Only this final marker makes the installation selectable.
    [IO.File]::WriteAllText((Join-Path $contentTarget '.fm4-plus-ready'), '1')
    [IO.File]::WriteAllText((Join-Path $target '.fm4-plus-ready'), "$Mode; update=$([bool]$UpdateZip)")
    # Publish only complete copies, both below their explicitly selected source roots.
    if ((Split-Path $target -Parent) -ne $GameDir.TrimEnd('\') -or (Split-Path $contentTarget -Parent) -ne $UserDir.TrimEnd('\')) { throw 'Invalid staging roots' }
    if ($preserved) {
        $shared = Join-Path $finalContentTarget '0000000000000000'
        $null = New-Item -ItemType Directory -Path $shared -Force
        $newContent = Join-Path $contentTarget '0000000000000000/4D530910'
        if (Test-Path -LiteralPath $newContent) { Copy-Item -LiteralPath $newContent -Destination $shared -Recurse }
        Copy-Item -LiteralPath (Join-Path $contentTarget '.fm4-plus.sha256') -Destination $finalContentTarget
        Copy-Item -LiteralPath (Join-Path $contentTarget '.fm4-plus-ready') -Destination $finalContentTarget
        Remove-Item -LiteralPath (Join-Path $finalContentTarget '.fm4-plus-preserved') -Force
        # This is our validated, freshly created staging directory only.
        if ((Split-Path $contentTarget -Parent) -ne $UserDir.TrimEnd('\') -or (Split-Path $contentTarget -Leaf) -notmatch '^\.pfp-[0-9a-f]{8}$') { throw 'Invalid cleanup staging path' }
        Remove-Item -LiteralPath $contentTarget -Recurse -Force
    } else { [IO.Directory]::Move($contentTarget, $finalContentTarget) }
    [IO.Directory]::Move($target, $finalTarget)
    Write-Output "Forza Plus installed ($Mode). Original files and saves preserved. $($skipped.Count) DLC packages omitted because they are not installed with headers."
} catch {
    # An incomplete copy is never selected. Keep it for diagnosis; do not delete user paths.
    Write-Output "Forza Plus: $($_.Exception.Message)"
    exit 1
} finally {
    foreach ($zip in $archives) { $zip.Dispose() }
}
