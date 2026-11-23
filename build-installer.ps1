param(
    [Parameter(Mandatory=$true)][string]$PackageDirectory,
    [string]$Version='',
    [string]$OutputDirectory='',
    [string]$Compiler='',
    [string]$Toolchain='',
    [switch]$Fixture,
    [switch]$LocalBuild,
    [switch]$Plan
)
$ErrorActionPreference='Stop'
$taskRoot=$PSScriptRoot
. (Join-Path $taskRoot 'scripts/build-env.ps1')
function Resolve-InstallerPath([string]$Path){
    if([IO.Path]::IsPathRooted($Path)){return [IO.Path]::GetFullPath($Path)}
    return [IO.Path]::GetFullPath((Join-Path $taskRoot $Path))
}
$PackageDirectory=Resolve-InstallerPath $PackageDirectory
if (-not $OutputDirectory) {
    $OutputDirectory = if ($LocalBuild) { 'releases-installer-local' } else { 'releases-installer' }
}
$OutputDirectory=Resolve-InstallerPath $OutputDirectory
if (-not $Version) {
    $match = Select-String -LiteralPath (Join-Path $taskRoot 'src/common/release_version.hpp') -Pattern '^#define KL1_RELEASE_VERSION "([^"]+)"$'
    if (-not $match) { throw 'Could not read the runtime version.' }
    $Version = $match.Matches[0].Groups[1].Value
}
if($Version -notmatch '^\d+(?:\.\d+){2,3}$'){throw 'Version requires three or four numeric components.'}
if(-not $Compiler){
    $taskFound=Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if($taskFound){$Compiler=$taskFound.Source}
    else{
        $taskCandidates=@()
        foreach($taskPrograms in @($env:ProgramFiles,${env:ProgramFiles(x86)})){
            if($taskPrograms){$taskCandidates+=Join-Path $taskPrograms 'Inno Setup 6/ISCC.exe';$taskCandidates+=Join-Path $taskPrograms 'Inno Setup 7/ISCC.exe'}
        }
        $Compiler=$taskCandidates|Where-Object {Test-Path -LiteralPath $_ -PathType Leaf}|Select-Object -First 1
    }
}
if ($Plan) {
    [ordered]@{version=$Version; package=$PackageDirectory; compiler=$Compiler; toolchain=$Toolchain;
               output=$OutputDirectory; fixture=[bool]$Fixture; localBuild=[bool]$LocalBuild; writesFiles=$false} | ConvertTo-Json
    return
}
if (-not $Compiler -or -not (Test-Path -LiteralPath $Compiler -PathType Leaf)) {
    throw 'Inno Setup compiler not found. Pass -Compiler <ISCC.exe>.'
}
$taskClang = Join-Path (Resolve-Toolchain $Toolchain) 'x86_64-w64-mingw32-clang++.exe'
$includeFlags = Get-SourceIncludeFlags -Root $taskRoot
$taskSources = @('build-installer.ps1', 'scripts/build-env.ps1', 'src/launcher/profile.hpp',
                 'vr-release.ini', 'LICENSE')
foreach ($folder in @('installer', 'docs', 'licenses', 'scripts', 'tools')) {
    $taskSources += Get-ChildItem -LiteralPath (Join-Path $taskRoot $folder) -File -Recurse |
        Where-Object { $_.Extension -in @('.ps1','.cmd','.py','.cpp','.iss','.md','.txt','.json') } |
        ForEach-Object { $_.FullName.Substring($taskRoot.Length + 1).Replace('\', '/') }
}
$taskSources = @($taskSources | Select-Object -Unique)
$taskSourceHashes=[ordered]@{}
foreach($taskSource in $taskSources){$taskSourceHashes[$taskSource]=(Get-FileHash -LiteralPath (Join-Path $taskRoot $taskSource)).Hash.ToLowerInvariant()}
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $taskRoot 'tests/installer/cleanup-previous-install-test.ps1')
if($LASTEXITCODE){throw 'Previous-install cleanup verification failed.'}
& python -X utf8 (Join-Path $taskRoot 'tests/installer/test_stage_release.py')
if($LASTEXITCODE){throw 'Release staging verification failed.'}
$taskStamp=[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff')+'-'+$PID
$taskStage=Join-Path $taskRoot ('.cache-build/installer-'+$taskStamp)
$taskPayload=Join-Path $taskStage 'payload'
$taskGuard=Join-Path $taskStage 'KaneLynchVRSetupGuard.exe'
$taskPublished=Join-Path $OutputDirectory ($Version+'-'+$taskStamp)
New-Item -ItemType Directory -Path $taskStage,$taskPublished|Out-Null
& $taskClang -std=c++17 -O2 -Wall -Wextra -Werror -static -municode @includeFlags (Join-Path $taskRoot 'installer/kl1_installer_guard.cpp') -o $taskGuard -lshlwapi -lbcrypt
if($LASTEXITCODE){throw 'Installer preflight helper compilation failed.'}
$taskPortable=Join-Path $taskPublished ('KaneLynchVR-'+$Version+'-Portable.zip')
$taskStageArgs=@('-X','utf8',(Join-Path $taskRoot 'installer/stage-release.py'),'--root',$taskRoot,'--package',$PackageDirectory,'--guard',$taskGuard,'--output',$taskPayload,'--version',$Version,'--portable',$taskPortable)
if($Fixture){$taskStageArgs+='--fixture'}
if($LocalBuild){$taskStageArgs+='--local-build'}
& python @taskStageArgs
if($LASTEXITCODE){throw 'Release payload staging failed.'}
$taskCompileArgs=@('/Qp',('/DPayloadDir='+$taskPayload),('/DModVersion='+$Version),('/DOutputDir='+$taskPublished))
if($Fixture){$taskCompileArgs+='/DInstallerSmoke=1'}
$taskCompileArgs+=Join-Path $taskRoot 'installer/KaneLynchVR.iss'
& $Compiler @taskCompileArgs
if($LASTEXITCODE){throw 'Inno Setup compilation failed.'}
foreach($taskSource in $taskSources){if((Get-FileHash -LiteralPath (Join-Path $taskRoot $taskSource)).Hash.ToLowerInvariant() -ne $taskSourceHashes[$taskSource]){throw ('Installer source changed during build: '+$taskSource)}}
$taskInstaller=Join-Path $taskPublished ('KaneLynchVR-'+$Version+'-Setup.exe')
$sourceCommit = $null
$sourceDirty = $null
if ((Test-Path -LiteralPath (Join-Path $taskRoot '.git')) -and (Get-Command git -ErrorAction SilentlyContinue)) {
    $sourceCommit = (& git -C $taskRoot rev-parse HEAD).Trim()
    $sourceDirty = [bool](& git -C $taskRoot status --porcelain)
}
$taskReport=[ordered]@{schemaVersion=1;product='Kane & Lynch: Dead Men VR';version=$Version;sourceCommit=$sourceCommit;sourceDirty=$sourceDirty;fixture=[bool]$Fixture;localBuild=[bool]$LocalBuild;
    installer=(Split-Path -Leaf $taskInstaller);installerSha256=(Get-FileHash -LiteralPath $taskInstaller -Algorithm SHA256).Hash.ToLowerInvariant();
    portable=(Split-Path -Leaf $taskPortable);portableSha256=(Get-FileHash -LiteralPath $taskPortable -Algorithm SHA256).Hash.ToLowerInvariant();
    payloadManifestSha256=(Get-FileHash -LiteralPath (Join-Path $taskPayload 'payload-manifest.json') -Algorithm SHA256).Hash.ToLowerInvariant();
    payload=$taskPayload;installerPath=$taskInstaller;portablePath=$taskPortable;createdUtc=[DateTime]::UtcNow.ToString('o');sourceSha256=$taskSourceHashes;recordingAutoStart=$false}
$taskBuildReport=Join-Path $taskStage 'installer-build.json'
[IO.File]::WriteAllText($taskBuildReport,($taskReport|ConvertTo-Json -Depth 5),[Text.UTF8Encoding]::new($false))
foreach($taskArtifact in @($taskInstaller,$taskPortable)){
    [IO.File]::WriteAllText(($taskArtifact+'.sha256'),((Get-FileHash -LiteralPath $taskArtifact -Algorithm SHA256).Hash.ToLowerInvariant()+'  '+(Split-Path -Leaf $taskArtifact)+"`n"),[Text.UTF8Encoding]::new($false))
}
$taskPublic=[ordered]@{schemaVersion=1;product='Kane & Lynch: Dead Men VR';version=$Version;fixture=[bool]$Fixture;
    payloadManifestSha256=$taskReport.payloadManifestSha256;
    files=@(@{file=$taskReport.installer;sha256=$taskReport.installerSha256},@{file=$taskReport.portable;sha256=$taskReport.portableSha256})}
[IO.File]::WriteAllText((Join-Path $taskPublished 'release.json'),($taskPublic|ConvertTo-Json -Depth 5),[Text.UTF8Encoding]::new($false))
$taskVerifyArgs=@('-X','utf8',(Join-Path $taskRoot 'installer/verify-release-assets.py'),$taskPublished)
if($Fixture){$taskVerifyArgs+='--allow-fixture'}
& python @taskVerifyArgs
if($LASTEXITCODE){throw 'Public release integrity/privacy verification failed.'}
Write-Output ('Installer ready: '+$taskInstaller)
Write-Output ('Portable ready: '+$taskPortable)
Write-Output ('Build report: '+$taskBuildReport)
