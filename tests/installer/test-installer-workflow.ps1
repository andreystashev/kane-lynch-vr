param(
    [Parameter(Mandatory=$true)][string]$BuildReport,
    [Parameter(Mandatory=$true)][string]$GameExecutable,
    [string]$PreviousPortable=''
)
$ErrorActionPreference='Stop'
$taskRoot=(Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$taskReport=[IO.File]::ReadAllText($BuildReport)|ConvertFrom-Json
if(-not $taskReport.fixture){throw 'Only the dedicated fixture AppId installer may be tested.'}
$taskPhysicalReloadDefault=if([version]$taskReport.version -ge [version]'0.3.64'){'1'}else{'0'}
$taskPhysicalReloadChoice=if($taskPhysicalReloadDefault -eq '1'){'0'}else{'1'}
$taskAppId='820E5F33-768D-4D59-9B77-92AC142D8BB4'
$taskRegistry='Software\Microsoft\Windows\CurrentVersion\Uninstall\{'+$taskAppId+'}_is1'
$taskRegistryBase=[Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::CurrentUser,[Microsoft.Win32.RegistryView]::Registry32)
$taskExisting=$taskRegistryBase.OpenSubKey($taskRegistry)
if($taskExisting){$taskExisting.Dispose();throw 'A previous fixture installation remains registered.'}
$taskFixture=Join-Path $taskRoot ('.cache-build/installer-workflow-'+[guid]::NewGuid().ToString('N'))
$taskFolder=Join-Path $taskFixture ('installed '+[char]0x0418+[char]0x0433+[char]0x0440+[char]0x044B+' with spaces')
$taskGame=Join-Path $taskFixture ('game '+[char]0x0418+[char]0x0433+[char]0x0440+[char]0x044B)
$taskUnsupported=Join-Path $taskFixture 'unsupported game'
New-Item -ItemType Directory -Path $taskFixture,$taskGame,$taskUnsupported|Out-Null
Copy-Item -LiteralPath $GameExecutable -Destination (Join-Path $taskGame 'KANEANDLYNCH.EXE')
[IO.File]::WriteAllText((Join-Path $taskGame 'graphics.ini'),"Resolution 3840x2160`r`nWindow 1`r`n")
New-Item -ItemType Directory -Path (Join-Path $taskGame 'Save')|Out-Null
[IO.File]::WriteAllBytes((Join-Path $taskGame 'Save/canary.sav'),[byte[]](1,2,3,4,5))
[IO.File]::WriteAllText((Join-Path $taskUnsupported 'KANEANDLYNCH.EXE'),'unsupported fixture')
$taskBefore=@{};Get-ChildItem -LiteralPath $taskGame -Recurse -File|ForEach-Object {$taskBefore[$_.FullName]=(Get-FileHash -LiteralPath $_.FullName).Hash}
$taskChecks=0;$taskHeld=$null;$taskEvent=$null;$taskInstalled=''
function Check([bool]$Condition,[string]$Label){if(-not $Condition){throw ('Installer workflow failed: '+$Label)};$script:taskChecks++}
$taskBoundary=@(
    @('Diagnostics','Enabled','0'),@('Debug','Invulnerable','0'),@('Debug','TestingModifiers','0'),
    @('Recording','Enabled','0'),@('VR','CaptureWithoutHost','0'),@('VR','ForcePanel','0'),
    @('VR','AutoStart','1'))
$taskAimingChoices=@(
    @('Weapons','NativeReticle','1'),@('Weapons','LaserSight','0'),
    @('Weapons','MountedReticle','0'),@('Weapons','ReticleStyle','0'))
function Ini-Value([string]$Text,[string]$Section,[string]$Key){
    $taskSection='';$taskValue=''
    foreach($taskLine in ($Text -split '\r?\n')){
        if($taskLine -match '^\s*\[([^\]]+)\]\s*$'){$taskSection=$Matches[1];continue}
        if($taskSection -ieq $Section -and $taskLine -match ('^\s*'+[regex]::Escape($Key)+'\s*=(.*)$')){$taskValue=$Matches[1].Trim()}
    };return $taskValue
}
function Patch-FixtureIni([string]$Text,[string]$Section,[string]$Key,[string]$Value){
    $taskLines=[Collections.Generic.List[string]]::new();foreach($taskLine in ($Text -split '\r?\n')){$taskLines.Add($taskLine)}
    $taskStart=-1;$taskEnd=$taskLines.Count
    for($taskIndex=0;$taskIndex -lt $taskLines.Count;$taskIndex++){
        if($taskLines[$taskIndex] -match '^\s*\[([^\]]+)\]\s*$'){
            if($taskStart -ge 0){$taskEnd=$taskIndex;break}
            if($Matches[1] -ieq $Section){$taskStart=$taskIndex}
        }
    }
    if($taskStart -lt 0){$taskLines.Add('['+$Section+']');$taskLines.Add($Key+'='+$Value)}
    else{
        $taskFound=$false
        for($taskIndex=$taskStart+1;$taskIndex -lt $taskEnd;$taskIndex++){
            if($taskLines[$taskIndex] -match ('^\s*'+[regex]::Escape($Key)+'\s*=')){$taskLines[$taskIndex]=$Key+'='+$Value;$taskFound=$true;break}
        }
        if(-not $taskFound){$taskLines.Insert($taskStart+1,$Key+'='+$Value)}
    };return [string]::Join("`r`n",$taskLines)
}
function Noisy-FixtureIni([string]$Text){
    foreach($taskSetting in $taskBoundary){
        if($taskSetting[1] -eq 'AutoStart'){continue}
        $taskValue=if($taskSetting[2] -eq '1'){'0'}else{'1'}
        $Text=Patch-FixtureIni $Text $taskSetting[0] $taskSetting[1] $taskValue
    };return $Text
}
function Check-ReleaseBoundary([string]$Folder,[string]$Phase){
    $taskText=[IO.File]::ReadAllText((Join-Path $Folder 'vr.ini'))
    foreach($taskSetting in $taskBoundary){Check ((Ini-Value $taskText $taskSetting[0] $taskSetting[1]) -eq $taskSetting[2]) ($Phase+' enforces '+$taskSetting[0]+'/'+$taskSetting[1])}
}
function Check-UserChoices([string]$Folder,[string]$Phase){
    $taskText=[IO.File]::ReadAllText((Join-Path $Folder 'vr.ini'))
    Check ((Ini-Value $taskText 'Controls' 'SmoothTurnDegreesPerSecond') -eq '120') ($Phase+' preserves user turn preference')
    Check ((Ini-Value $taskText 'Fixture' 'CustomSetting') -eq '42') ($Phase+' preserves custom setting')
    Check ((Ini-Value $taskText 'Connection' 'Mode') -eq 'SteamVR') ($Phase+' preserves selected connection')
    Check ((Ini-Value $taskText 'Connection' 'RuntimeJson') -eq $script:taskRuntimeFixture) ($Phase+' preserves Unicode connection path')
    Check ((Ini-Value $taskText 'Fixture' 'UnicodeNote') -eq $script:taskUnicodeFixture) ($Phase+' preserves Unicode custom value')
    Check ((Ini-Value $taskText 'Weapons' 'PhysicalReload') -eq $taskPhysicalReloadChoice) ($Phase+' preserves explicit physical reload choice')
    Check ((Ini-Value $taskText 'HUD' 'GameHints') -eq '0') ($Phase+' preserves chosen game hints')
    foreach($taskChoice in $taskAimingChoices){
        Check ((Ini-Value $taskText $taskChoice[0] $taskChoice[1]) -eq $taskChoice[2]) ($Phase+' preserves '+$taskChoice[1])
    }
    Check ((Ini-Value $taskText 'HUD' 'Immersive') -eq '0') ($Phase+' preserves chosen HUD presentation')
}
function Run-Child([string]$Exe,[string[]]$Arguments){
    $taskChild=Start-Process -FilePath $Exe -ArgumentList $Arguments -WindowStyle Hidden -PassThru
    if(-not $taskChild.WaitForExit(45000)){throw ('Owned fixture timed out: '+$taskChild.Id)}
    return $taskChild.ExitCode
}
function Setup-Args([string]$Directory,[string]$Game,[string]$Name){
    $taskArgs=@('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART',('/FIXTUREAPPID='+$taskAppId),('/DIR="'+$Directory+'"'),('/GAMEEXE="'+$Game+'"'),'/TASKS=desktopicon',('/LOG="'+(Join-Path $taskFixture ($Name+'.log'))+'"'))
    return $taskArgs
}
function Uninstall-Current([string]$Directory,[string]$Label){
    $taskUnins=Get-ChildItem -LiteralPath $Directory -Filter 'unins*.exe'|Select-Object -First 1
    if($taskUnins){return Run-Child $taskUnins.FullName @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART',('/LOG="'+(Join-Path $taskFixture ($Label+'.log'))+'"'))}
    return -1
}
try{
    $taskInstallerSource=[IO.File]::ReadAllText((Join-Path $taskRoot 'installer/KaneLynchVR.iss'))
    Check ($taskInstallerSource -notmatch 'PreviousPage|CreateInputQueryPage|\{param:PREVIOUSDIR') 'installer has no previous-folder wizard or manual previous-path override'
    Check ($taskInstallerSource.Contains('DeleteFile(CleanupSnapshot); DeleteFile(DiscoveryFile);')) 'each preflight retry clears prior ownership/discovery state'
    $taskManifest=[IO.File]::ReadAllText((Join-Path $taskReport.payload 'payload-manifest.json'))|ConvertFrom-Json
    Check ((Get-FileHash -LiteralPath $taskReport.installerPath).Hash -ieq $taskReport.installerSha256) 'installer SHA256 matches build report'
    $taskForeign=Join-Path $taskFixture 'another mod with common runtime names';New-Item -ItemType Directory -Path $taskForeign|Out-Null
    [IO.File]::WriteAllText((Join-Path $taskForeign 'LaunchVR.exe'),'ANOTHER MOD LAUNCHER KEEP')
    [IO.File]::WriteAllText((Join-Path $taskForeign 'openxr_loader.dll'),'ANOTHER MOD OPENXR KEEP')
    [IO.File]::WriteAllText((Join-Path $taskForeign 'payload-manifest.json'),'{"schemaVersion":1,"product":"Hitman 2 VR","version":"1.0.0","files":[]}')
    $taskForeignHashes=@{};Get-ChildItem -LiteralPath $taskForeign -File|ForEach-Object {$taskForeignHashes[$_.FullName]=(Get-FileHash -LiteralPath $_.FullName).Hash}
    Check ((Run-Child $taskReport.installerPath (Setup-Args $taskForeign (Join-Path $taskGame 'KANEANDLYNCH.EXE') 'foreign-mod-target')) -ne 0) 'foreign mod destination with shared runtime names is refused'
    foreach($taskEntry in $taskForeignHashes.GetEnumerator()){Check ((Get-FileHash -LiteralPath $taskEntry.Key).Hash -eq $taskEntry.Value) 'foreign runtime/manifest canary unchanged'}
    Check (-not(Test-Path -LiteralPath (Join-Path $taskForeign 'KaneLynchVR.dll'))) 'foreign target received no KL1 payload'
    $taskCode=Run-Child $taskReport.installerPath (Setup-Args $taskFolder (Join-Path $taskGame 'KANEANDLYNCH.EXE') 'install')
    Check ($taskCode -eq 0) 'fresh Unicode installation succeeds';$taskInstalled=$taskFolder
    foreach($taskFile in $taskManifest.files){
        Check (Test-Path -LiteralPath (Join-Path $taskFolder $taskFile.file) -PathType Leaf) ('installed '+$taskFile.file)
        if($taskFile.file -notin @('vr.ini','launcher.ini')){Check ((Get-FileHash -LiteralPath (Join-Path $taskFolder $taskFile.file)).Hash -ieq $taskFile.sha256) ('installed bytes '+$taskFile.file)}
    }
    Check ((Run-Child (Join-Path $taskFolder 'LaunchVR.exe') @('--self-test')) -eq 0) 'installed launcher self-test works without starting the game'
    $taskConfig=[IO.File]::ReadAllText((Join-Path $taskFolder 'vr.ini'))
    $taskSelection=[IO.File]::ReadAllText((Join-Path $taskFolder 'launcher.ini'))
    $taskSaved=[regex]::Match($taskSelection,'(?m)^GameExe=(.*)').Groups[1].Value.Trim()
    Check (-not [IO.Path]::IsPathRooted($taskSaved) -and [IO.Path]::GetFullPath((Join-Path $taskFolder $taskSaved)) -eq (Join-Path $taskGame 'KANEANDLYNCH.EXE')) 'selected Unicode game path saved relative to installed launcher'
    Check ($taskConfig -match '(?m)^AutoStart=1\s*$') 'VR starts automatically'
    Check ((Ini-Value $taskConfig 'Diagnostics' 'Enabled') -eq '0' -and (Ini-Value $taskConfig 'Recording' 'Enabled') -eq '0') 'release logs/recorder are disabled'
    Check-ReleaseBoundary $taskFolder 'fresh install'
    if([version]$taskReport.version -ge [version]'0.3.62'){
        Check ((Ini-Value $taskConfig 'VR' 'DXVKGPUTransport') -eq '1') 'fresh installs enable direct DXVK GPU transport'
        Check ((Ini-Value $taskConfig 'VR' 'GPUSharedD3D9') -eq '1') 'fresh installs enable shared Direct3D 9 images'
        Check ((Ini-Value $taskConfig 'Weapons' 'PhysicalReload') -eq $taskPhysicalReloadDefault) 'fresh installs use the versioned physical reload default'
        Check ((Ini-Value $taskConfig 'HUD' 'GameHints') -eq '1') 'fresh installs have native game hints enabled'
        Check ((Ini-Value $taskConfig 'HUD' 'Immersive') -eq '1') 'fresh installs use the adapted HUD'
    }
    $taskOversize=Join-Path $taskFixture 'oversize local config';New-Item -ItemType Directory -Path $taskOversize|Out-Null
    $taskLargeConfig=Join-Path $taskOversize 'launcher.ini';[IO.File]::WriteAllBytes($taskLargeConfig,(New-Object byte[] 4194305))
    $taskLargeHash=(Get-FileHash -LiteralPath $taskLargeConfig).Hash
    Check ((Run-Child (Join-Path $taskReport.payload 'KaneLynchVRSetupGuard.exe') @('--save-game-selection',('"'+$taskOversize+'"'),('"'+(Join-Path $taskGame 'KANEANDLYNCH.EXE')+'"'))) -eq 5) 'oversized local config is refused before allocation'
    Check ((Get-FileHash -LiteralPath $taskLargeConfig).Hash -eq $taskLargeHash) 'refused oversized config remains intact'
    foreach($taskShortcut in @('Kane & Lynch Dead Men VR Installer Test','Kane & Lynch Dead Men VR Installer Test Settings')){
        foreach($taskLocation in @([Environment]::GetFolderPath('Programs'),[Environment]::GetFolderPath('Desktop'))){Check (Test-Path -LiteralPath (Join-Path $taskLocation ($taskShortcut+'.lnk'))) ('fixture shortcut '+$taskShortcut)}
    }
    # In-place update must remove only an obsolete manifest-owned file. New,
    # custom and recorded content must remain.
    $taskObsolete=Join-Path $taskFolder 'CHANGES-RU.md';[IO.File]::WriteAllText($taskObsolete,'obsolete unchanged owned file')
    $taskOldManifest=[IO.File]::ReadAllText((Join-Path $taskFolder 'payload-manifest.json'))|ConvertFrom-Json
    $taskOldManifest.files+=@{file='CHANGES-RU.md';sha256=(Get-FileHash -LiteralPath $taskObsolete).Hash.ToLowerInvariant()}
    [IO.File]::WriteAllText((Join-Path $taskFolder 'payload-manifest.json'),($taskOldManifest|ConvertTo-Json -Depth 6),[Text.UTF8Encoding]::new($false))
    $taskUnicodeFixture=[string][char]0x0421+[char]0x043E+[char]0x0445+[char]0x0440+[char]0x0430+[char]0x043D+[char]0x0435+[char]0x043D+[char]0x043E
    $taskRuntimeFixture='..\runtime-'+$taskUnicodeFixture+'.json'
    $taskConfig=Noisy-FixtureIni $taskConfig
    foreach($taskSetting in @(@('Fixture','CustomSetting','42'),@('Controls','SmoothTurnDegreesPerSecond','120'),@('Connection','Mode','SteamVR'),@('Connection','RuntimeJson',$taskRuntimeFixture),@('Fixture','UnicodeNote',$taskUnicodeFixture))){$taskConfig=Patch-FixtureIni $taskConfig $taskSetting[0] $taskSetting[1] $taskSetting[2]}
    $taskConfig=Patch-FixtureIni $taskConfig 'Weapons' 'PhysicalReload' $taskPhysicalReloadChoice
    foreach($taskChoice in $taskAimingChoices){
        $taskConfig=Patch-FixtureIni $taskConfig $taskChoice[0] $taskChoice[1] $taskChoice[2]
    }
    $taskConfig=Patch-FixtureIni $taskConfig 'HUD' 'GameHints' '0'
    $taskConfig=Patch-FixtureIni $taskConfig 'HUD' 'Immersive' '0'
    $taskConfig=Patch-FixtureIni $taskConfig 'VR' 'DXVKGPUTransport' '0'
    $taskConfig=Patch-FixtureIni $taskConfig 'VR' 'GPUSharedD3D9' '0'
    [IO.File]::WriteAllText((Join-Path $taskFolder 'vr.ini'),$taskConfig,[Text.Encoding]::Unicode)
    Add-Content -LiteralPath (Join-Path $taskFolder 'launcher.ini') -Value "`r`n[Fixture]`r`nCustomLauncherSetting=kept" -Encoding Unicode
    [IO.File]::WriteAllText((Join-Path $taskFolder 'user-file.txt'),'private user file')
    New-Item -ItemType Directory -Path (Join-Path $taskFolder 'recordings')|Out-Null
    [IO.File]::WriteAllText((Join-Path $taskFolder 'recordings/private.mp4'),'private recording')
    $taskEventName='Local\KaneLynchVR-Installer-Test-'+[guid]::NewGuid().ToString('N')
    $taskEvent=[Threading.EventWaitHandle]::new($false,[Threading.EventResetMode]::ManualReset,$taskEventName)
    $taskHeld=Start-Process -FilePath (Join-Path $taskFolder 'KaneLynchVRSetupGuard.exe') -ArgumentList @('--hold-test-event',$taskEventName) -WindowStyle Hidden -PassThru
    Start-Sleep -Milliseconds 150
    Check ((Run-Child (Join-Path $taskReport.payload 'KaneLynchVRSetupGuard.exe') @('--check-install-path',('"'+$taskFolder+'"'))) -eq 4) 'running owned helper blocks replacement'
    $taskCode=Run-Child $taskReport.installerPath (Setup-Args $taskFolder (Join-Path $taskGame 'KANEANDLYNCH.EXE') 'busy')
    Check ($taskCode -ne 0) 'busy update refused'
    Check ((Uninstall-Current $taskFolder 'busy-uninstall') -ne 0) 'busy uninstall refused'
    $taskHeld.Refresh();Check (-not $taskHeld.HasExited) 'installer never terminates running mod'
    [void]$taskEvent.Set();Check ($taskHeld.WaitForExit(5000)) 'owned helper exits through event';$taskHeld=$null
    $taskCode=Run-Child $taskReport.installerPath (Setup-Args $taskFolder (Join-Path $taskGame 'KANEANDLYNCH.EXE') 'upgrade')
    Check ($taskCode -eq 0) 'same-directory upgrade succeeds'
    Check (-not(Test-Path -LiteralPath $taskObsolete)) 'in-place upgrade removes obsolete verified file'
    Check (Test-Path -LiteralPath (Join-Path $taskFolder 'LaunchVR.exe')) 'in-place cleanup preserves new launcher'
    Check-ReleaseBoundary $taskFolder 'in-place dev upgrade'
    Check-UserChoices $taskFolder 'in-place dev upgrade'
    Check ((Ini-Value ([IO.File]::ReadAllText((Join-Path $taskFolder 'vr.ini'))) 'VR' 'DXVKGPUTransport') -eq '0') 'in-place upgrade preserves explicit DXVK GPU transport opt-out'
    Check ((Ini-Value ([IO.File]::ReadAllText((Join-Path $taskFolder 'vr.ini'))) 'VR' 'GPUSharedD3D9') -eq '0') 'in-place upgrade preserves explicit shared Direct3D 9 image opt-out'
    Check ([IO.File]::ReadAllText((Join-Path $taskFolder 'launcher.ini')).Contains('CustomLauncherSetting=kept')) 'game selection update preserves unrelated launcher settings'
    $taskNested=Join-Path $taskFolder 'nested mod'
    Check ((Run-Child $taskReport.installerPath (Setup-Args $taskNested (Join-Path $taskGame 'KANEANDLYNCH.EXE') 'nested')) -ne 0) 'nested relocation refused'
    # A migrated dev profile can be UTF-8 rather than the fresh UTF-16 INI.
    # Re-enable every diagnostic before moving; release boundaries must win.
    $taskConfig=Noisy-FixtureIni ([IO.File]::ReadAllText((Join-Path $taskFolder 'vr.ini')))
    $taskConfig=$taskConfig -replace '(?im)^[ \t]*(?:DXVKGPUTransport|GPUSharedD3D9)[ \t]*=[^\r\n]*(?:\r?\n|$)',''
    [IO.File]::WriteAllText((Join-Path $taskFolder 'vr.ini'),$taskConfig,[Text.UTF8Encoding]::new($false))
    $taskOther=Join-Path $taskFixture 'relocated separate mod'
    $taskCode=Run-Child $taskReport.installerPath (Setup-Args $taskOther (Join-Path $taskGame 'KANEANDLYNCH.EXE') 'relocate')
    Check ($taskCode -eq 0) 'relocation succeeds';$taskInstalled=$taskOther
    Check-ReleaseBoundary $taskOther 'UTF-8 dev relocation'
    Check-UserChoices $taskOther 'UTF-8 dev relocation'
    if([version]$taskReport.version -ge [version]'0.3.62'){
        Check ((Ini-Value ([IO.File]::ReadAllText((Join-Path $taskOther 'vr.ini'))) 'VR' 'DXVKGPUTransport') -eq '1') 'relocation enables direct DXVK GPU transport when the old profile lacks the key'
        Check ((Ini-Value ([IO.File]::ReadAllText((Join-Path $taskOther 'vr.ini'))) 'VR' 'GPUSharedD3D9') -eq '1') 'relocation enables shared Direct3D 9 images when the old profile lacks the key'
    }
    $taskEncoded=[IO.File]::ReadAllBytes((Join-Path $taskOther 'vr.ini'))
    Check ($taskEncoded[0] -eq 255 -and $taskEncoded[1] -eq 254) 'migrated UTF-8 settings become a WinAPI-safe Unicode INI'
    foreach($taskName in @('LaunchVR.exe','KaneLynchVR.dll','KaneLynchVRAudio.dll','KaneLynchVRHost.exe','KaneLynchVRMonitor.exe','KaneLynchOpenXRHost.exe')){Check (-not(Test-Path -LiteralPath (Join-Path $taskFolder $taskName))) ('old runtime cleaned '+$taskName)}
    Check (Test-Path -LiteralPath (Join-Path $taskFolder 'recordings/private.mp4')) 'relocation retains original recording'
    Check (Test-Path -LiteralPath (Join-Path $taskFolder 'user-file.txt')) 'relocation retains original unknown file'
    Check ((Run-Child $taskReport.installerPath (Setup-Args (Join-Path $taskFixture 'unsupported target') (Join-Path $taskUnsupported 'KANEANDLYNCH.EXE') 'unsupported')) -ne 0) 'unsupported game executable refused'
    Check ((Run-Child $taskReport.installerPath (Setup-Args (Join-Path $taskGame 'bad mod') (Join-Path $taskGame 'KANEANDLYNCH.EXE') 'game-directory')) -ne 0) 'installation inside game folder refused'
    Check ((Uninstall-Current $taskOther 'uninstall') -eq 0) 'uninstall succeeds';$taskInstalled=''
    Check (-not(Test-Path -LiteralPath (Join-Path $taskOther 'KaneLynchVR.dll'))) 'uninstall removes mod runtime'
    Check (Test-Path -LiteralPath (Join-Path $taskOther 'vr.ini')) 'uninstall keeps user settings'
    # A portable copy at the chosen destination needs no registry entry or
    # extra input. Its manifest is captured before replacement for cleanup.
    $taskKey=$taskRegistryBase.OpenSubKey($taskRegistry);Check ($null -eq $taskKey) 'fixture uninstall removed registration';if($taskKey){$taskKey.Dispose()}
    $taskPortable=Join-Path $taskFixture 'unregistered portable destination'
    if($PreviousPortable){
        $taskArchive=(Resolve-Path -LiteralPath $PreviousPortable).Path
        $taskAssets=Split-Path -Parent $taskArchive
        & python -X utf8 (Join-Path $taskRoot 'installer/verify-release-assets.py') $taskAssets
        if($LASTEXITCODE){throw 'Previous release assets failed verification.'}
        $taskHistorical=[IO.File]::ReadAllText((Join-Path $taskAssets 'release.json'))|ConvertFrom-Json
        if((Split-Path -Leaf $taskArchive) -cne ('KaneLynchVR-'+$taskHistorical.version+'-Portable.zip')){
            throw 'Pass the verified Portable ZIP from the release folder.'
        }
        Expand-Archive -LiteralPath $taskArchive -DestinationPath $taskPortable
    }else{
        Copy-Item -LiteralPath $taskReport.payload -Destination $taskPortable -Recurse
    }
    $taskHadLegacyShortcut=Test-Path -LiteralPath (Join-Path $taskPortable 'Run-VR.cmd')
    $taskPortableObsolete=Join-Path $taskPortable 'CHANGES-RU.md';[IO.File]::WriteAllText($taskPortableObsolete,'obsolete portable owned bytes')
    $taskPortableManifest=[IO.File]::ReadAllText((Join-Path $taskPortable 'payload-manifest.json'))|ConvertFrom-Json
    $taskPortableManifest.files+=@{file='CHANGES-RU.md';sha256=(Get-FileHash -LiteralPath $taskPortableObsolete).Hash.ToLowerInvariant()}
    [IO.File]::WriteAllText((Join-Path $taskPortable 'payload-manifest.json'),($taskPortableManifest|ConvertTo-Json -Depth 6),[Text.UTF8Encoding]::new($false))
    $taskPortableIni=Join-Path $taskPortable 'vr.ini'
    if($taskPhysicalReloadDefault -eq '1'){
        $taskConfig=[IO.File]::ReadAllText($taskPortableIni) -replace '(?im)^[ \t]*PhysicalReload[ \t]*=[^\r\n]*(?:\r?\n|$)',''
        Check ((Ini-Value $taskConfig 'Weapons' 'PhysicalReload') -eq '') 'portable upgrade starts with a missing physical reload setting'
        [IO.File]::WriteAllText($taskPortableIni,$taskConfig,[Text.Encoding]::Unicode)
    }
    Add-Content -LiteralPath $taskPortableIni -Value "`r`n[Fixture]`r`nPortableSetting=kept" -Encoding Unicode
    [IO.File]::WriteAllText((Join-Path $taskPortable 'portable-personal.txt'),'portable private user file')
    Check ((Run-Child $taskReport.installerPath (Setup-Args $taskPortable (Join-Path $taskGame 'KANEANDLYNCH.EXE') 'portable-in-place')) -eq 0) 'unregistered portable destination discovered automatically';$taskInstalled=$taskPortable
    Check (-not(Test-Path -LiteralPath $taskPortableObsolete)) 'automatic portable in-place cleanup removes obsolete verified file'
    if($taskHadLegacyShortcut){
        Check (-not(Test-Path -LiteralPath (Join-Path $taskPortable 'Run-VR.cmd'))) 'upgrade removes the old manifest-owned shortcut'
    }
    Check (Test-Path -LiteralPath (Join-Path $taskPortable 'LICENSE') -PathType Leaf) 'upgrade installs the project license'
    $taskConfig=[IO.File]::ReadAllText($taskPortableIni)
    Check ($taskConfig.Contains('PortableSetting=kept')) 'automatic portable update preserves custom setting'
    if($taskPhysicalReloadDefault -eq '1'){
        Check ((Ini-Value $taskConfig 'Weapons' 'PhysicalReload') -eq '1') 'in-place update enables physical reload when the existing profile lacks the key'
    }
    Check (Test-Path -LiteralPath (Join-Path $taskPortable 'portable-personal.txt')) 'automatic portable cleanup preserves unknown file'
    Check ((Uninstall-Current $taskPortable 'portable-uninstall') -eq 0) 'portable replacement uninstalls';$taskInstalled=''
    # Broken or unsafe registrations cannot make the installer inspect all
    # processes under a broad unrelated folder or demand deletion permissions.
    foreach($taskBadPrevious in @($env:USERPROFILE,(Join-Path $taskFixture 'missing previous folder'))){
        $taskBadKey=$taskRegistryBase.CreateSubKey($taskRegistry)
        try{$taskBadKey.SetValue('InstallLocation',$taskBadPrevious);$taskBadKey.SetValue('UninstallString',('"'+(Join-Path $taskBadPrevious 'unins000.exe')+'"'))}finally{$taskBadKey.Dispose()}
        $taskFresh=Join-Path $taskFixture ('automatic fresh '+[guid]::NewGuid().ToString('N'))
        Check ((Run-Child $taskReport.installerPath (Setup-Args $taskFresh (Join-Path $taskGame 'KANEANDLYNCH.EXE') 'stale-registry')) -eq 0) 'unsafe or missing previous registration does not block installation';$taskInstalled=$taskFresh
        Check (Test-Path -LiteralPath (Join-Path $taskFresh 'KaneLynchVR.dll')) 'fresh runtime installed after ignored registration'
        Check ((Uninstall-Current $taskFresh 'stale-registry-uninstall') -eq 0) 'fresh replacement uninstalls after ignored registration';$taskInstalled=''
    }
    foreach($taskEntry in $taskBefore.GetEnumerator()){Check ((Get-FileHash -LiteralPath $taskEntry.Key).Hash -eq $taskEntry.Value) 'fixture game and save bytes unchanged'}
    Check ((Get-FileHash -LiteralPath $GameExecutable).Hash -eq (Get-FileHash -LiteralPath (Join-Path $taskGame 'KANEANDLYNCH.EXE')).Hash) 'actual source game executable never changed'
    Write-Output ("Installer workflow PASS: $taskChecks checks; private fixture AppId, Unicode paths, install/update/relocate/uninstall, no game launched.")
    Write-Output ('Evidence: '+$taskFixture)
}finally{
    if($taskHeld){if($taskEvent){[void]$taskEvent.Set()};[void]$taskHeld.WaitForExit(5000)}
    if($taskEvent){$taskEvent.Dispose()}
    if($taskInstalled){[void](Uninstall-Current $taskInstalled 'finally-uninstall')}
    $taskRegistryBase.Dispose()
}
