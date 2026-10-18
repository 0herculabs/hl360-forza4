param([Parameter(Mandatory=$true)][string]$Installer, [Parameter(Mandatory=$true)][string]$OutputDir)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$root = Join-Path $OutputDir ([Guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $root -Force
function WriteFile($path, $text) {
    $null = New-Item -ItemType Directory -Path (Split-Path $path -Parent) -Force
    [IO.File]::WriteAllText($path, $text)
}
function Zip($path, $files) {
    $z = [IO.Compression.ZipFile]::Open($path, 'Create')
    try { foreach ($name in $files.Keys) {
        $e = $z.CreateEntry($name)
        $w = [IO.StreamWriter]::new($e.Open())
        try { $w.Write($files[$name]) } finally { $w.Dispose() }
    } } finally { $z.Dispose() }
}
function Assert($condition, $message) { if (!$condition) { throw $message } }
$game = Join-Path $root 'game'
$user = Join-Path $root 'user'
WriteFile "$game/default.xex" 'original executable'
WriteFile "$game/Media/db/gamedb.slt" 'original database'
WriteFile "$game/Media/RenderScenarios/1-Car_Race.xml" 'port shadows'
WriteFile "$user/PROFILE/4D530910/00000001/ForzaProfile" 'original save'
$title = '0000000000000000/4D530910'
$package = '4d53091000000001'
$longPackage = '062414787A814070FA177E1C472BA36D3884B1134D'
WriteFile "$user/$title/Headers/00000002/$longPackage.header" 'existing DLC header'
WriteFile "$user/$title/Headers/00000002/$package.header" 'original license'
WriteFile "$user/$title/00000002/$package/Media/old.bin" 'original content'
$p = 'Project Forza Plus 4/'
$baseFiles = @{
    ($p+'Main Game Changes/default.xex') = 'incompatible executable';
    ($p+'Main Game Changes/Media/db/gamedb.slt') = 'base normal';
    ($p+'Main Game Changes/Media/RenderScenarios/1-Car_Race.xml') = 'xenia shadows';
    ($p+"Disc 2 and DLCs/00000002/$package/Media/data.bin") = 'mod content';
    ($p+"Disc 2 and DLCs/00000002/$longPackage/Media/27.puboffer") = '42-character package';
    ($p+'Disc 2 and DLCs/00000002/319143B00C605B32FE7622B5C0089E17296AA0E44D/Media/37.puboffer') = 'uninstalled DLC';
    ($p+'Disc 2 and DLCs/00000002/4d53091000000002/Media/data.bin') = 'missing license'
}
foreach ($mode in @('Normal','Hard','Sandbox','Vanilla')) { $baseFiles[$p+"Game Mode DBs/$mode Mode/gamedb.slt"] = "base $mode" }
$baseZip = Join-Path $root 'base.zip'
Zip $baseZip $baseFiles
$updateZip = Join-Path $root 'update.zip'
$u = 'Project Forza Plus 4 Update/Update Files/'
$updateFiles = @{}
foreach ($mode in @('Normal','Hard','Sandbox','Vanilla')) { $updateFiles[$u+"Game Mode DBs/$mode Mode/gamedb.slt"] = "update $mode" }
$updateFiles[$u+'physics/PhysicsSettings.ini'] = 'updated physics'
$updateFiles[$u+'audio/AudioEngineConfig.xml'] = 'updated audio'
Zip $updateZip $updateFiles
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Installer -BaseZip $baseZip -UpdateZip $updateZip -GameDir $game -UserDir $user -Mode Hard
Assert ($LASTEXITCODE -eq 0) 'Installation failed'
Assert ((Get-Content "$game/.fm4-plus/Media/db/gamedb.slt" -Raw) -eq 'update Hard') 'Selected mode/update precedence failed'
Assert ((Get-Content "$game/.fm4-plus/Media/audio/AudioEngineConfig.xml" -Raw) -eq 'updated audio') 'Audio update ignored'
Assert ((Get-Content "$game/.fm4-plus/Media/physics/PhysicsSettings.ini" -Raw) -eq 'updated physics') 'Physics update ignored'
Assert ((Get-Content "$game/.fm4-plus/default.xex" -Raw) -eq 'original executable') 'Executable replaced'
Assert ((Get-Content "$game/.fm4-plus/Media/RenderScenarios/1-Car_Race.xml" -Raw) -eq 'port shadows') 'Render workaround replaced'
Assert ((Get-Content "$game/Media/db/gamedb.slt" -Raw) -eq 'original database') 'Original modified'
Assert (!(Test-Path "$user/.fm4-plus/PROFILE")) 'Save copied into mod installation'
Assert (!(Test-Path "$user/.fm4-plus/$title/00000002/4d53091000000002")) 'Missing-license package installed'
Assert ((Get-Content "$user/.fm4-plus/$title/Headers/00000002/$package.header" -Raw) -eq 'original license') 'License modified'
Assert ((Get-Content "$user/.fm4-plus/$title/00000002/$longPackage/Media/27.puboffer" -Raw) -eq '42-character package') '42-character DLC package rejected'
Assert (!(Test-Path "$user/.fm4-plus/$title/00000002/319143B00C605B32FE7622B5C0089E17296AA0E44D")) 'Uninstalled 42-character package not skipped'
Assert (Test-Path "$game/.fm4-plus/.fm4-plus-ready") 'Completion marker missing'
Assert (Test-Path "$user/.fm4-plus/.fm4-plus.sha256") 'Content manifest missing'
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Installer -BaseZip $baseZip -GameDir $game -UserDir $user
Assert ($LASTEXITCODE -ne 0) 'Existing installation overwritten'
foreach ($mode in @('Normal','Sandbox','Vanilla')) {
    $g = Join-Path $root $mode; $usr = Join-Path $root ($mode+'User')
    WriteFile "$g/default.xex" 'original executable'
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Installer -BaseZip $baseZip -GameDir $g -UserDir $usr -Mode $mode
    Assert ($LASTEXITCODE -eq 0) "Mode $mode failed"
    Assert ((Get-Content "$g/.fm4-plus/Media/db/gamedb.slt" -Raw) -eq "base $mode") "Mode $mode ignored"
}
$badGame = Join-Path $root 'badGame'; WriteFile "$badGame/default.xex" 'original'
$baseFiles[$p+'Main Game Changes/Media/../../escape.bin'] = 'escape'
$badZip = Join-Path $root 'bad.zip'; Zip $badZip $baseFiles
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Installer -BaseZip $badZip -GameDir $badGame -UserDir (Join-Path $root 'badUser')
Assert ($LASTEXITCODE -ne 0) 'Archive traversal accepted'
Assert (!(Test-Path "$badGame/.fm4-plus/.fm4-plus-ready")) 'Failed install marked ready'
Assert (!(Test-Path "$badGame/escape.bin")) 'Archive escaped destination'
$uninstaller = Join-Path (Split-Path $Installer -Parent) 'uninstall-forza-plus.ps1'
WriteFile "$user/.fm4-plus/PROFILE/4D530910/00000001/ForzaProfile" 'mod career save'
WriteFile "$user/.fm4-plus/cache_partition/keep.dat" 'mod cache'
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $uninstaller -GameDir $game -UserDir $user
Assert ($LASTEXITCODE -eq 0) 'Mod uninstall failed'
Assert (!(Test-Path "$game/.fm4-plus")) 'Mod game files remain after uninstall'
Assert (!(Test-Path "$user/.fm4-plus/$title")) 'Mod content remains after uninstall'
Assert (!(Test-Path "$user/.fm4-plus/.fm4-plus-ready")) 'Uninstalled mod still marked ready'
Assert ((Get-Content "$user/.fm4-plus/PROFILE/4D530910/00000001/ForzaProfile" -Raw) -eq 'mod career save') 'Mod save deleted'
Assert ((Get-Content "$user/PROFILE/4D530910/00000001/ForzaProfile" -Raw) -eq 'original save') 'Original save deleted'
Assert ((Get-Content "$game/default.xex" -Raw) -eq 'original executable') 'Original game deleted'
Assert (Test-Path "$user/$title/Headers/00000002/$package.header") 'Original content deleted'
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Installer -BaseZip $baseZip -GameDir $game -UserDir $user -Mode Sandbox
Assert ($LASTEXITCODE -eq 0) 'Reinstall after uninstall failed'
Assert ((Get-Content "$game/.fm4-plus/Media/db/gamedb.slt" -Raw) -eq 'base Sandbox') 'Reinstalled career mode not changed'
Assert ((Get-Content "$user/.fm4-plus/PROFILE/4D530910/00000001/ForzaProfile" -Raw) -eq 'mod career save') 'Reinstall overwrote mod save'
Assert ((Get-Content "$user/.fm4-plus/cache_partition/keep.dat" -Raw) -eq 'mod cache') 'Reinstall overwrote user cache'
$outside = Join-Path $root 'outside';WriteFile "$outside/keep.txt" 'outside data'
$link = Join-Path $game '.fm4-plus/linked'
$null = New-Item -ItemType Junction -Path $link -Target $outside
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $uninstaller -GameDir $game -UserDir $user
Assert ($LASTEXITCODE -ne 0) 'Uninstaller accepted a junction'
Assert ((Get-Content "$outside/keep.txt" -Raw) -eq 'outside data') 'Uninstall escaped mod root'
Assert (Test-Path "$game/.fm4-plus/.fm4-plus-ready") 'Rejected uninstall modified installation'
[IO.Directory]::Delete($link)
Write-Output 'Forza Plus install/uninstall/reinstall: modes, saves, original files, licenses, manifests, traversal and junction checks passed.'
