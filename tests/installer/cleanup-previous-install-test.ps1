$ErrorActionPreference='Stop'
$taskRoot=(Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$taskFixture=Join-Path $taskRoot ('.cache-build/previous-cleanup-'+[guid]::NewGuid().ToString('N'))
$taskGameFolder=Join-Path $taskFixture 'game';$taskGame=Join-Path $taskGameFolder 'KANEANDLYNCH.EXE'
New-Item -ItemType Directory -Path $taskFixture,$taskGameFolder -Force|Out-Null
[IO.File]::WriteAllText($taskGame,'game-canary NEVER CHANGE')
$taskId='820E5F33-768D-4D59-9B77-92AC142D8BB4'
$taskChecks=0
function Check([bool]$Value,[string]$Label){if(-not $Value){throw ('Cleanup test failed: '+$Label)};$script:taskChecks++}
function New-Folder([string]$Name){$taskPath=Join-Path $taskFixture $Name;New-Item -ItemType Directory -Path $taskPath -Force|Out-Null;return $taskPath}
function Write-File([string]$Path,[string]$Value){[IO.File]::WriteAllText($Path,$Value,(New-Object Text.UTF8Encoding($false)))}
function New-Owned([string]$Name){
    $taskPath=New-Folder $Name
    Write-File (Join-Path $taskPath 'LaunchVR.exe') 'owned fixture exe'
    Write-File (Join-Path $taskPath 'KaneLynchVR.dll') 'owned fixture dll'
    Write-File (Join-Path $taskPath 'KaneLynchVRHost.exe') 'owned fixture host'
    Write-File (Join-Path $taskPath 'README.md') 'owned fixture readme'
    Write-File (Join-Path $taskPath 'mod.ini') "[HUD]`nCrosshairs=1"
    $taskRecords=@();foreach($taskFile in @('LaunchVR.exe','KaneLynchVR.dll','KaneLynchVRHost.exe','README.md','mod.ini')){$taskRecords+=@{file=$taskFile;sha256=(Get-FileHash -LiteralPath (Join-Path $taskPath $taskFile)).Hash.ToLowerInvariant()}}
    Write-File (Join-Path $taskPath 'payload-manifest.json') (@{schemaVersion=1;product='Kane & Lynch: Dead Men VR';version='0.13.13';sourceCommit=('a'*40);files=$taskRecords}|ConvertTo-Json -Depth 5)
    return $taskPath
}
function Run-Cleanup([string]$Phase,[string]$Old,[string]$New,[string]$Uninstaller='', [bool]$ExpectFailure=$false){
    $taskResult=Join-Path $taskFixture ('result-'+[guid]::NewGuid().ToString('N')+'.json')
    $taskFailed=$false
    try{& (Join-Path $taskRoot 'installer/cleanup-previous-install.ps1') -Phase $Phase -PreviousDirectory $Old -NewDirectory $New -GameExe $taskGame -AppId $taskId -UninstallerPath $Uninstaller -ResultFile $taskResult|Out-Null}catch{$taskFailed=$true}
    Check ($taskFailed -eq $ExpectFailure) ('expected exit status '+$Phase)
    return Get-Content -LiteralPath $taskResult -Raw|ConvertFrom-Json
}
function Write-Uninstaller([string]$Folder,[string]$Id,[string]$BoundFolder){
    $taskExe=Join-Path $Folder 'unins000.exe';Write-File $taskExe 'MZ owned uninstaller fixture'
    $taskBytes=New-Object byte[] 192
    [Array]::Copy([Text.Encoding]::ASCII.GetBytes('Inno Setup Uninstall Log (b)'),0,$taskBytes,0,28)
    $taskIdBytes=[Text.Encoding]::ASCII.GetBytes('{'+$Id+'}');[Array]::Copy($taskIdBytes,0,$taskBytes,64,$taskIdBytes.Length)
    $taskBytes+=[Text.Encoding]::Unicode.GetBytes($BoundFolder+'\LaunchVR.exe')
    [IO.File]::WriteAllBytes((Join-Path $Folder 'unins000.dat'),$taskBytes)
    return $taskExe
}
$taskOriginalGameHash=(Get-FileHash -LiteralPath $taskGame).Hash
$taskOld=New-Owned 'old mod with spaces';$taskNew=Join-Path $taskFixture 'new mod with spaces'
Write-File (Join-Path $taskOld 'vr.ini') "[VR]`nCustom=42";Write-File (Join-Path $taskOld 'launcher.ini') "[Launcher]`nConnection=VDXR"
Write-File (Join-Path $taskOld 'user-private.txt') 'UNKNOWN MUST SURVIVE';New-Item -ItemType Directory -Path (Join-Path $taskOld 'recordings')|Out-Null;Write-File (Join-Path $taskOld 'recordings/private.mp4') 'RECORDING MUST SURVIVE'
$taskUnins=Write-Uninstaller $taskOld $taskId $taskOld
Write-File (Join-Path $taskOld 'unins001.exe') 'unrelated uninstaller must remain'
$taskBefore=(Get-FileHash -LiteralPath (Join-Path $taskOld 'mod.ini')).Hash
$taskResult=Run-Cleanup Preflight $taskOld $taskNew $taskUnins
Check ($taskResult.recognizedPrevious -and $taskResult.ownership -eq 'manifest') 'recognizes coherent public payload before writing'
Check (-not(Test-Path -LiteralPath $taskNew)) 'preflight never creates destination'
$taskResult=Run-Cleanup Migrate $taskOld $taskNew $taskUnins
Check ($taskResult.migrated.Count -eq 3) 'copies exactly three supported config files'
Check ((Get-FileHash -LiteralPath (Join-Path $taskNew 'mod.ini')).Hash -eq $taskBefore) 'migration preserves config bytes'
Write-File (Join-Path $taskNew 'mod.ini') 'DESTINATION CHOICE MUST SURVIVE'
$taskResult=Run-Cleanup Migrate $taskOld $taskNew $taskUnins
Check ((Get-Content -LiteralPath (Join-Path $taskNew 'mod.ini') -Raw) -eq 'DESTINATION CHOICE MUST SURVIVE') 'never overwrites destination config'
Write-File (Join-Path $taskOld 'README.md') 'USER CHANGED MOD DOCUMENT'
$taskResult=Run-Cleanup Cleanup $taskOld $taskNew $taskUnins $true
Check (Test-Path -LiteralPath (Join-Path $taskOld 'LaunchVR.exe')) 'cannot remove old runtime before replacement is installed'
$taskInstalledNew=New-Owned 'verified new payload'
$taskVerifiedNew=Join-Path $taskFixture 'verified new payload'
$taskBadNew=New-Owned 'corrupted replacement';Write-File (Join-Path $taskBadNew 'KaneLynchVRHost.exe') 'changed host'
$taskResult=Run-Cleanup Cleanup $taskOld $taskBadNew $taskUnins $true
Check (Test-Path -LiteralPath (Join-Path $taskOld 'LaunchVR.exe')) 'modified replacement core prevents any old cleanup'
$taskNew=$taskVerifiedNew
$taskResult=Run-Cleanup Cleanup $taskOld $taskNew $taskUnins
Check (-not(Test-Path -LiteralPath (Join-Path $taskOld 'LaunchVR.exe'))) 'removes manifest-owned unchanged old runtime'
Check (-not(Test-Path -LiteralPath (Join-Path $taskOld 'KaneLynchVR.dll'))) 'removes second owned old runtime'
Check ((Get-Content -LiteralPath (Join-Path $taskOld 'README.md') -Raw) -eq 'USER CHANGED MOD DOCUMENT') 'retains changed owned file'
Check ((Get-FileHash -LiteralPath (Join-Path $taskOld 'mod.ini')).Hash -eq $taskBefore) 'retains original settings'
Check (Test-Path -LiteralPath (Join-Path $taskOld 'recordings/private.mp4')) 'retains recordings'
Check (Test-Path -LiteralPath (Join-Path $taskOld 'user-private.txt')) 'retains unrelated user file'
Check (-not(Test-Path -LiteralPath $taskUnins) -and -not(Test-Path -LiteralPath ([IO.Path]::ChangeExtension($taskUnins,'.dat')))) 'removes only verified registered uninstaller pair'
Check (Test-Path -LiteralPath (Join-Path $taskOld 'unins001.exe')) 'does not wildcard delete other uninstallers'
Check (Test-Path -LiteralPath $taskOld -PathType Container) 'never recursively removes previous directory'
$taskLocked=New-Owned 'locked uninstall pair';$taskLockedExe=Write-Uninstaller $taskLocked $taskId $taskLocked
$taskLock=[IO.File]::Open($taskLockedExe,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
try{
    $taskResult=Run-Cleanup Cleanup $taskLocked $taskVerifiedNew $taskLockedExe
    Check ((Test-Path -LiteralPath $taskLockedExe) -and (Test-Path -LiteralPath ([IO.Path]::ChangeExtension($taskLockedExe,'.dat')))) 'locked old uninstaller preserves its DAT pair'
}finally{$taskLock.Dispose()}
$taskResult=Run-Cleanup Preflight (Join-Path $taskFixture 'deleted old folder') (Join-Path $taskFixture 'fresh target')
Check $taskResult.stalePrevious 'stale registration does not prevent installation'
$taskUnknown=New-Folder 'unknown user folder';Write-File (Join-Path $taskUnknown 'mod.ini') 'UNRECOGNIZED';Write-File (Join-Path $taskUnknown 'LaunchVR.exe') 'unknown application'
$taskUnknownNew=Join-Path $taskFixture 'unknown fresh target'
$taskResult=Run-Cleanup Migrate $taskUnknown $taskUnknownNew
Check (-not $taskResult.recognizedPrevious -and -not(Test-Path -LiteralPath $taskUnknownNew)) 'unverified user folder contributes no settings'
$taskResult=Run-Cleanup Cleanup $taskUnknown $taskUnknownNew
Check ((Get-Content -LiteralPath (Join-Path $taskUnknown 'LaunchVR.exe') -Raw) -eq 'unknown application') 'unverified application is untouched'
foreach($taskUnsafePrevious in @('relative-folder',[IO.Path]::GetPathRoot($taskFixture),$env:USERPROFILE,$env:WINDIR,$taskGameFolder)){
    $taskResult=Run-Cleanup Cleanup $taskUnsafePrevious (Join-Path $taskFixture 'safe new')
    Check $taskResult.skippedUnsafePrevious 'unsafe previous registration is ignored without mutation'
}

foreach($taskLicenseFolder in @((New-Folder 'foreign license only'),(New-Owned 'old mod with unrelated license'))){
    $taskLicense=Join-Path $taskLicenseFolder 'LICENSE'
    Write-File $taskLicense 'UNRELATED LICENSE MUST SURVIVE'
    $taskResult=Run-Cleanup Preflight '' $taskLicenseFolder '' $true
    Check (-not $taskResult.success -and (Get-Content -LiteralPath $taskLicense -Raw) -eq 'UNRELATED LICENSE MUST SURVIVE') 'preflight preserves a LICENSE outside the previous manifest'
}
$taskOtherGame=New-Folder 'other Kane and Lynch Dead Men installation';Write-File (Join-Path $taskOtherGame 'KANEANDLYNCH.EXE') 'OTHER GAME COPY MUST SURVIVE'
$taskResult=Run-Cleanup Cleanup $taskOtherGame (Join-Path $taskFixture 'safe new')
Check $taskResult.skippedUnsafePrevious 'another Kane & Lynch: Dead Men game folder is ignored even when it is not selected'
$taskResult=Run-Cleanup Preflight '' (Join-Path $taskOtherGame 'nested mod') '' $true
Check (-not $taskResult.success -and (Get-Content -LiteralPath (Join-Path $taskOtherGame 'KANEANDLYNCH.EXE') -Raw) -eq 'OTHER GAME COPY MUST SURVIVE') 'cannot install within another game copy or change that copy'
foreach($taskUnsafeNew in @([IO.Path]::GetPathRoot($taskFixture),$env:USERPROFILE,$env:WINDIR,$taskGameFolder,(Join-Path $taskGameFolder 'mod'),$taskFixture)){
    $taskResult=Run-Cleanup Preflight '' $taskUnsafeNew '' $true
    Check (-not $taskResult.success) 'unsafe new destination is refused'
}
$taskOverlap=New-Owned 'overlap old'
$taskResult=Run-Cleanup Preflight $taskOverlap (Join-Path $taskOverlap 'nested') '' $true
Check (-not $taskResult.success) 'nested relocation is refused'
$taskResult=Run-Cleanup Cleanup $taskOverlap $taskOverlap
Check ($taskResult.sameDirectory -and (Test-Path -LiteralPath (Join-Path $taskOverlap 'LaunchVR.exe'))) 'same-folder installation never deletes newly installed runtime'
$taskMalicious=New-Owned 'malicious manifest'
$taskManifest=Get-Content -LiteralPath (Join-Path $taskMalicious 'payload-manifest.json') -Raw|ConvertFrom-Json
$taskManifest.files+=@{file='../game/KANEANDLYNCH.EXE';sha256=$taskOriginalGameHash}
Write-File (Join-Path $taskMalicious 'payload-manifest.json') ($taskManifest|ConvertTo-Json -Depth 5)
$taskResult=Run-Cleanup Cleanup $taskMalicious (Join-Path $taskFixture 'safe new')
Check ($taskResult.skippedUnsafePrevious -and (Test-Path -LiteralPath (Join-Path $taskMalicious 'LaunchVR.exe'))) 'unsafe manifest is rejected as a whole before deletion'
$taskUnknownEntry=New-Owned 'unknown entry manifest'
Write-File (Join-Path $taskUnknownEntry 'user-canary.txt') 'PRIVATE UNKNOWN';$taskManifest=Get-Content -LiteralPath (Join-Path $taskUnknownEntry 'payload-manifest.json') -Raw|ConvertFrom-Json
$taskManifest.files+=@{file='user-canary.txt';sha256=(Get-FileHash -LiteralPath (Join-Path $taskUnknownEntry 'user-canary.txt')).Hash}
Write-File (Join-Path $taskUnknownEntry 'payload-manifest.json') ($taskManifest|ConvertTo-Json -Depth 5)
$taskResult=Run-Cleanup Cleanup $taskUnknownEntry $taskVerifiedNew
Check ((Test-Path -LiteralPath (Join-Path $taskUnknownEntry 'user-canary.txt')) -and -not(Test-Path -LiteralPath (Join-Path $taskUnknownEntry 'LaunchVR.exe'))) 'unknown manifest file stays even if its hash matches'
$taskDuplicate=New-Owned 'duplicate manifest'
$taskManifest=Get-Content -LiteralPath (Join-Path $taskDuplicate 'payload-manifest.json') -Raw|ConvertFrom-Json
$taskManifest.files+=@{file='LAUNCHVR.EXE';sha256=$taskManifest.files[0].sha256}
Write-File (Join-Path $taskDuplicate 'payload-manifest.json') ($taskManifest|ConvertTo-Json -Depth 5)
$taskResult=Run-Cleanup Cleanup $taskDuplicate $taskVerifiedNew
Check ($taskResult.skippedUnsafePrevious -and (Test-Path -LiteralPath (Join-Path $taskDuplicate 'LaunchVR.exe'))) 'case-duplicate manifest cannot authorize deletion'
$taskForgery=New-Folder 'forged uninstall ownership';$taskForged=Write-Uninstaller $taskForgery '11111111-1111-1111-1111-111111111111' $taskForgery
$taskResult=Run-Cleanup Cleanup $taskForgery (Join-Path $taskFixture 'safe new') $taskForged
Check (-not $taskResult.recognizedPrevious -and (Test-Path -LiteralPath $taskForged)) 'different uninstall AppId is retained'
$taskForged=Write-Uninstaller $taskForgery $taskId (Join-Path $taskFixture 'other binding')
$taskResult=Run-Cleanup Cleanup $taskForgery (Join-Path $taskFixture 'safe new') $taskForged
Check (-not $taskResult.recognizedPrevious -and (Test-Path -LiteralPath $taskForged)) 'uninstall record for another folder is retained'
$taskDatOwned=New-Folder 'legacy owned by DAT';$taskLegacyUnins=Write-Uninstaller $taskDatOwned $taskId $taskDatOwned;Write-File (Join-Path $taskDatOwned 'mod.ini') 'LEGACY USER CHOICE';Write-File (Join-Path $taskDatOwned 'LaunchVR.exe') 'NO MANIFEST UNKNOWN HASH'
$taskResult=Run-Cleanup Migrate $taskDatOwned (Join-Path $taskFixture 'legacy target') $taskLegacyUnins
Check ($taskResult.recognizedPrevious -and $taskResult.migrated.Count -eq 1) 'verified legacy uninstall record permits safe config migration'
$taskResult=Run-Cleanup Cleanup $taskDatOwned $taskVerifiedNew $taskLegacyUnins
Check (Test-Path -LiteralPath (Join-Path $taskDatOwned 'LaunchVR.exe')) 'legacy binary without manifest identity remains'
$taskLarge=New-Owned 'oversize config';[IO.File]::WriteAllBytes((Join-Path $taskLarge 'vr.ini'),(New-Object byte[] 4194305))
$taskResult=Run-Cleanup Migrate $taskLarge (Join-Path $taskFixture 'large target')
Check (-not(Test-Path -LiteralPath (Join-Path $taskFixture 'large target/vr.ini'))) 'oversized config is retained without copying'
$taskHardlink=New-Owned 'hardlinked source config'
New-Item -ItemType HardLink -Path (Join-Path $taskHardlink 'vr.ini') -Target ([WildcardPattern]::Escape($taskGame))|Out-Null
$taskResult=Run-Cleanup Migrate $taskHardlink (Join-Path $taskFixture 'hardlink target')
Check (-not(Test-Path -LiteralPath (Join-Path $taskFixture 'hardlink target/vr.ini'))) 'hardlinked external content is not imported as a config'
# Junctions are created only below this ignored fixture and removed as links.
$taskLink=Join-Path $taskFixture 'junction old';New-Item -ItemType Junction -Path $taskLink -Target ([WildcardPattern]::Escape($taskGameFolder))|Out-Null
try{$taskResult=Run-Cleanup Cleanup $taskLink (Join-Path $taskFixture 'safe new');Check $taskResult.skippedUnsafePrevious 'old root junction is refused';$taskResult=Run-Cleanup Preflight '' (Join-Path $taskLink 'mod') '' $true;Check (-not $taskResult.success) 'new target through a game junction is refused'}finally{[IO.Directory]::Delete($taskLink)}
$taskSubLink=New-Owned 'subdir junction';$taskLink=Join-Path $taskSubLink 'renderer-dxvk-x86';New-Item -ItemType Junction -Path $taskLink -Target ([WildcardPattern]::Escape($taskGameFolder))|Out-Null
$taskManifest=Get-Content -LiteralPath (Join-Path $taskSubLink 'payload-manifest.json') -Raw|ConvertFrom-Json;$taskManifest.files+=@{file='renderer-dxvk-x86/d3d9.dll';sha256=('b'*64)};Write-File (Join-Path $taskSubLink 'payload-manifest.json') ($taskManifest|ConvertTo-Json -Depth 5)
try{$taskResult=Run-Cleanup Cleanup $taskSubLink (Join-Path $taskFixture 'safe new');Check ($taskResult.skippedUnsafePrevious -and (Test-Path -LiteralPath (Join-Path $taskSubLink 'LaunchVR.exe'))) 'junction manifest subdirectory blocks all old cleanup'}finally{[IO.Directory]::Delete($taskLink)}
# In-place updates use a verified preflight snapshot, then skip every current
# payload filename even if old and new bytes happen to be identical.
$taskInPlace=New-Owned 'in-place obsolete cleanup'
Write-File (Join-Path $taskInPlace 'CHANGES-RU.md') 'obsolete owned changelog'
$taskManifest=Get-Content -LiteralPath (Join-Path $taskInPlace 'payload-manifest.json') -Raw|ConvertFrom-Json
$taskManifest.files+=@{file='CHANGES-RU.md';sha256=(Get-FileHash -LiteralPath (Join-Path $taskInPlace 'CHANGES-RU.md')).Hash}
Write-File (Join-Path $taskInPlace 'payload-manifest.json') ($taskManifest|ConvertTo-Json -Depth 5)
$taskSnapshot=Join-Path $taskFixture 'in-place-snapshot.json'
& (Join-Path $taskRoot 'installer/cleanup-previous-install.ps1') -Phase Preflight -PreviousDirectory $taskInPlace -NewDirectory $taskInPlace -GameExe $taskGame -AppId $taskId -SnapshotFile $taskSnapshot|Out-Null
Check (Test-Path -LiteralPath $taskSnapshot) 'verified in-place ownership snapshot is created before replacement'
foreach($taskFile in Get-ChildItem -LiteralPath $taskVerifiedNew -File){Copy-Item -LiteralPath $taskFile.FullName -Destination (Join-Path $taskInPlace $taskFile.Name) -Force}
$taskResultText=& (Join-Path $taskRoot 'installer/cleanup-previous-install.ps1') -Phase Cleanup -PreviousDirectory $taskInPlace -NewDirectory $taskInPlace -GameExe $taskGame -AppId $taskId -SnapshotFile $taskSnapshot
$taskResult=($taskResultText -join [Environment]::NewLine)|ConvertFrom-Json
Check (-not(Test-Path -LiteralPath (Join-Path $taskInPlace 'CHANGES-RU.md'))) 'in-place update removes only obsolete unchanged owned file'
Check ((Test-Path -LiteralPath (Join-Path $taskInPlace 'LaunchVR.exe')) -and (Test-Path -LiteralPath (Join-Path $taskInPlace 'README.md'))) 'identical current payload files survive in-place cleanup'
Check (Test-Path -LiteralPath (Join-Path $taskInPlace 'payload-manifest.json')) 'replacement ownership manifest survives in-place cleanup'
# Older portable releases have the same digest ownership in BUILD-INFO.json.
$taskPortable=New-Owned 'portable older release'
$taskManifest=Get-Content -LiteralPath (Join-Path $taskPortable 'payload-manifest.json') -Raw|ConvertFrom-Json
$taskFiles=[ordered]@{};foreach($taskFile in $taskManifest.files){$taskFiles[$taskFile.file]=$taskFile.sha256}
Write-File (Join-Path $taskPortable 'BUILD-INFO.json') (@{repository='kane-lynch-vr';version='0.13.13';sourceHead=('a'*40);files=$taskFiles}|ConvertTo-Json -Depth 5)
Remove-Item -LiteralPath (Join-Path $taskPortable 'payload-manifest.json')
$taskResult=Run-Cleanup Preflight $taskPortable (Join-Path $taskFixture 'portable target')
Check $taskResult.recognizedPrevious 'older portable BUILD-INFO release is recognized by hashes'
$taskResult=Run-Cleanup Cleanup $taskPortable $taskVerifiedNew
Check (-not(Test-Path -LiteralPath (Join-Path $taskPortable 'KaneLynchVR.dll'))) 'verified portable old runtime is removed after replacement exists'
Check ((Get-FileHash -LiteralPath $taskGame).Hash -eq $taskOriginalGameHash) 'game canary remains identical through every migration/cleanup/refusal'
# The new renderer participates in the same exact-hash ownership cleanup.
$taskOn12=New-Owned 'old On12 preview'
$taskOn12Manifest=Get-Content -LiteralPath (Join-Path $taskOn12 'payload-manifest.json') -Raw|ConvertFrom-Json
$taskOn12Names=@('renderer-d3d9on12-x86/d3d9.dll','renderer-d3d9on12-x86/NOTICE.txt','renderer-d3d9on12-x86/provenance.json')
foreach($taskName in $taskOn12Names){
    $taskPath=Join-Path $taskOn12 $taskName;New-Item -ItemType Directory -Force (Split-Path $taskPath)|Out-Null
    Write-File $taskPath ('owned '+$taskName)
    $taskOn12Manifest.files+=@([pscustomobject]@{file=$taskName;sha256=(Get-FileHash -LiteralPath $taskPath).Hash.ToLowerInvariant()})
}
Write-File (Join-Path $taskOn12 'payload-manifest.json') ($taskOn12Manifest|ConvertTo-Json -Depth 6)
Write-File (Join-Path $taskOn12 'renderer-d3d9on12-x86/provenance.json') 'user edited renderer helper'
Write-File (Join-Path $taskOn12 'renderer-d3d9on12-x86/custom.ini') 'unknown user file'
$taskResult=Run-Cleanup Cleanup $taskOn12 $taskVerifiedNew
Check (-not(Test-Path -LiteralPath (Join-Path $taskOn12 'renderer-d3d9on12-x86/d3d9.dll'))) 'owned On12 DLL removed after replacement verification'
Check (-not(Test-Path -LiteralPath (Join-Path $taskOn12 'renderer-d3d9on12-x86/NOTICE.txt'))) 'owned On12 notice removed after replacement verification'
Check ((Get-Content -LiteralPath (Join-Path $taskOn12 'renderer-d3d9on12-x86/provenance.json') -Raw) -eq 'user edited renderer helper') 'edited On12 helper preserved'
Check (Test-Path -LiteralPath (Join-Path $taskOn12 'renderer-d3d9on12-x86/custom.ini')) 'unknown On12 file preserved'
# Relocation to a different parent must preserve the effective custom runtime,
# rather than retaining a relative string whose meaning changes with {app}.
$taskRuntimeOld=New-Owned 'custom runtime old';$taskRuntimeParent=New-Folder 'runtime provider'
$taskRuntimeTarget=Join-Path $taskRuntimeParent ('custom '+[char]0x0418+[char]0x0433+[char]0x0440+[char]0x044B+'.json')
Write-File $taskRuntimeTarget '{"runtime":{"library_path":"runtime.dll"}}'
$taskRuntimeRelative='..\runtime provider\'+[IO.Path]::GetFileName($taskRuntimeTarget)
Write-File (Join-Path $taskRuntimeOld 'vr.ini') ("[Connection]`r`nRuntimeJson="+$taskRuntimeRelative+"`r`n")
$taskRuntimeBefore=(Get-FileHash -LiteralPath (Join-Path $taskRuntimeOld 'vr.ini')).Hash
$taskRuntimeCanary=(Get-FileHash -LiteralPath $taskRuntimeTarget).Hash
$taskDeep=New-Folder 'different parent';$taskRuntimeNew=Join-Path $taskDeep 'nested install'
$taskResult=Run-Cleanup Migrate $taskRuntimeOld $taskRuntimeNew
$taskMigratedRuntime=[regex]::Match([IO.File]::ReadAllText((Join-Path $taskRuntimeNew 'vr.ini')),'(?m)^RuntimeJson=(.*)').Groups[1].Value.Trim()
$taskEffective=if([IO.Path]::IsPathRooted($taskMigratedRuntime)){[IO.Path]::GetFullPath($taskMigratedRuntime)}else{[IO.Path]::GetFullPath((Join-Path $taskRuntimeNew $taskMigratedRuntime))}
Check ([string]::Equals($taskEffective,$taskRuntimeTarget,[StringComparison]::OrdinalIgnoreCase)) 'custom relative runtime target preserved across changed parent'
Check (-not [IO.Path]::IsPathRooted($taskMigratedRuntime)) 'same-volume migrated runtime remains relative'
Check ((Get-FileHash -LiteralPath (Join-Path $taskRuntimeOld 'vr.ini')).Hash -eq $taskRuntimeBefore) 'rebasing never edits old settings'
Check ((Get-FileHash -LiteralPath $taskRuntimeTarget).Hash -eq $taskRuntimeCanary) 'rebasing never writes to custom runtime file'
Write-File (Join-Path $taskRuntimeNew 'vr.ini') "[Connection]`r`nRuntimeJson=..\destination-preference.json`r`n"
$taskDestinationHash=(Get-FileHash -LiteralPath (Join-Path $taskRuntimeNew 'vr.ini')).Hash
$taskResult=Run-Cleanup Migrate $taskRuntimeOld $taskRuntimeNew
Check ((Get-FileHash -LiteralPath (Join-Path $taskRuntimeNew 'vr.ini')).Hash -eq $taskDestinationHash) 'existing destination runtime preference is never rebased'
# The recorder's executable lives outside {app}; its script path still blocks
# replacement/uninstall. This owned event fixture starts no game or recorder.
$taskBusy=New-Owned 'busy interpreter mod';$taskBusyScript=Join-Path $taskBusy 'owned-wait.ps1'
Write-File $taskBusyScript 'param([string]$EventName); $e=[Threading.EventWaitHandle]::OpenExisting($EventName); try{[void]$e.WaitOne(10000)}finally{$e.Dispose()}'
$taskBusyEventName='Local\KL1-Installer-Interpreter-'+[guid]::NewGuid().ToString('N')
$taskBusyEvent=[Threading.EventWaitHandle]::new($false,[Threading.EventResetMode]::ManualReset,$taskBusyEventName)
$taskBusyProcess=Start-Process -FilePath (Join-Path $PSHOME 'powershell.exe') -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-File',('"'+$taskBusyScript+'"'),'-EventName',$taskBusyEventName) -WindowStyle Hidden -PassThru
try {
    Start-Sleep -Milliseconds 350
    $taskIdleFailed=$false
    try{& (Join-Path $taskRoot 'installer/cleanup-previous-install.ps1') -Phase Idle -NewDirectory $taskBusy|Out-Null}catch{$taskIdleFailed=$true}
    Check $taskIdleFailed 'active interpreter script blocks uninstall idle check'
    $taskBusyProcess.Refresh();Check (-not $taskBusyProcess.HasExited) 'idle check never stops an active process'
}finally{[void]$taskBusyEvent.Set();Check ($taskBusyProcess.WaitForExit(5000)) 'owned interpreter exits through event';$taskBusyEvent.Dispose()}
# A selected folder is not permission to replace another mod's common names.
$taskForeign=New-Folder 'foreign mod target'
Write-File (Join-Path $taskForeign 'LaunchVR.exe') 'FOREIGN LAUNCHER MUST SURVIVE'
Write-File (Join-Path $taskForeign 'openxr_loader.dll') 'FOREIGN OPENXR MUST SURVIVE'
Write-File (Join-Path $taskForeign 'vr.ini') '[Foreign] Keep=1'
$taskForeignFiles=@();Get-ChildItem -LiteralPath $taskForeign -File|ForEach-Object {$taskForeignFiles+=@{file=$_.Name;sha256=(Get-FileHash -LiteralPath $_.FullName).Hash.ToLowerInvariant()}}
Write-File (Join-Path $taskForeign 'payload-manifest.json') (@{schemaVersion=1;product='Hitman 2 VR';version='1.0.0';files=$taskForeignFiles}|ConvertTo-Json -Depth 5)
$taskForeignBefore=@{};Get-ChildItem -LiteralPath $taskForeign -File|ForEach-Object {$taskForeignBefore[$_.FullName]=(Get-FileHash -LiteralPath $_.FullName).Hash}
$taskResult=Run-Cleanup Preflight '' $taskForeign '' $true
foreach($taskEntry in $taskForeignBefore.GetEnumerator()){Check ((Get-FileHash -LiteralPath $taskEntry.Key).Hash -eq $taskEntry.Value) 'foreign destination refused without overwriting any canary'}
$taskUnknownCollision=New-Folder 'unmarked runtime target';Write-File (Join-Path $taskUnknownCollision 'openxr_loader.dll') 'UNMARKED RUNTIME MUST SURVIVE'
$taskResult=Run-Cleanup Preflight '' $taskUnknownCollision '' $true
Check ((Get-Content -LiteralPath (Join-Path $taskUnknownCollision 'openxr_loader.dll') -Raw) -eq 'UNMARKED RUNTIME MUST SURVIVE') 'unmarked common runtime cannot be overwritten'
$taskPersonal=New-Folder 'noncolliding personal target';Write-File (Join-Path $taskPersonal 'personal-notes.txt') 'PERSONAL NOTES MUST SURVIVE'
$taskResult=Run-Cleanup Preflight '' $taskPersonal
Check $taskResult.success 'noncolliding personal files do not force an empty target folder'
Check ((Get-Content -LiteralPath (Join-Path $taskPersonal 'personal-notes.txt') -Raw) -eq 'PERSONAL NOTES MUST SURVIVE') 'noncolliding personal file unchanged'
$taskKnownTarget=New-Owned 'known KL1 portable target'
$taskResult=Run-Cleanup Preflight '' $taskKnownTarget
Check $taskResult.success 'verified same-destination KL1 portable remains upgradable'
Write-Output ("Previous-install cleanup: $taskChecks checks passed; isolated files, no registry/game/runtime modification.")
