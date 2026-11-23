param(
    [Parameter(Mandatory=$true)][string]$GuardExecutable,
    [Parameter(Mandatory=$true)][string[]]$SupportedGameExe
)
$ErrorActionPreference='Stop'
$taskRoot=(Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$taskFixture=Join-Path $taskRoot ('.cache-build/installer-game-verifier-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $taskFixture|Out-Null
$taskChecks=0
function Check([bool]$Value,[string]$Name){if(-not $Value){throw ('Installer verifier failed: '+$Name)};$script:taskChecks++}
function Snapshot-File([string]$Path){$i=Get-Item -LiteralPath $Path;return @{sha=(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash;size=$i.Length;written=$i.LastWriteTimeUtc.Ticks;attributes=[int]$i.Attributes}}
function Run-Guard([string[]]$Arguments){
    $psi=[Diagnostics.ProcessStartInfo]::new();$psi.FileName=[IO.Path]::GetFullPath($GuardExecutable)
    $psi.Arguments=[string]::Join(' ',$Arguments);$psi.UseShellExecute=$false;$psi.CreateNoWindow=$true
    $psi.RedirectStandardOutput=$true;$psi.RedirectStandardError=$true;$psi.WorkingDirectory=$taskFixture
    $child=[Diagnostics.Process]::Start($psi)
    try{if(-not $child.WaitForExit(10000)){throw 'Private verifier timed out'};return @{code=$child.ExitCode;output=$child.StandardOutput.ReadToEnd();error=$child.StandardError.ReadToEnd()}}finally{$child.Dispose()}
}
$taskSources=@{};$taskIndex=0
foreach($taskSource in $SupportedGameExe){
    $taskSources[$taskSource]=Snapshot-File $taskSource
    $taskFolder=Join-Path $taskFixture ('game '+[char]0x0418+[char]0x0433+[char]0x0440+[char]0x044B+' '+$taskIndex)
    New-Item -ItemType Directory -Path $taskFolder|Out-Null
    $taskCopy=Join-Path $taskFolder 'KANEANDLYNCH.EXE';Copy-Item -LiteralPath $taskSource -Destination $taskCopy
    [IO.File]::SetAttributes($taskCopy,[IO.FileAttributes]::ReadOnly)
    $taskCopyBefore=Snapshot-File $taskCopy
    $r=Run-Guard @('--verify-game',('"'+$taskCopy+'"'))
    Check ($r.code -in @(0,10)) 'supported private readonly image accepted'
    Check ($r.output -match '^Supported:' -and $r.output.Contains(('SHA256: '+$taskSources[$taskSource].sha.ToLowerInvariant()))) 'actual full raw SHA reported'
    $taskOriginal=[IO.File]::ReadAllBytes($taskCopy)
    foreach($taskCase in @('timestamp','checksum','metadata-combined','code','layout','truncated','overlay')){
        $taskMutated=Join-Path $taskFixture ('case '+$taskIndex+' '+$taskCase);New-Item -ItemType Directory -Path $taskMutated|Out-Null
        $taskMutated=Join-Path $taskMutated 'KANEANDLYNCH.EXE';$taskBytes=[byte[]]$taskOriginal.Clone();$taskExpected=3
        switch($taskCase){
            'timestamp'{$taskBytes[0x138]=$taskBytes[0x138] -bxor 0x55;$taskExpected=10}
            'checksum'{$taskBytes[0x188]=$taskBytes[0x188] -bxor 0x55;$taskExpected=10}
            'metadata-combined'{$taskBytes[0x138]=$taskBytes[0x138] -bxor 0x13;$taskBytes[0x188]=$taskBytes[0x188] -bxor 0x77;$taskBytes[0x146]=$taskBytes[0x146] -bxor 0x20;$taskExpected=10}
            'code'{$taskBytes[0x1000]=$taskBytes[0x1000] -bxor 1}
            'layout'{$taskBytes[0x13C]=$taskBytes[0x13C] -bxor 1}
            'truncated'{$taskShort=New-Object byte[] ($taskBytes.Length-1);[Array]::Copy($taskBytes,$taskShort,$taskShort.Length);$taskBytes=$taskShort}
            'overlay'{$taskBytes+=[byte]1}
        }
        [IO.File]::WriteAllBytes($taskMutated,$taskBytes);[IO.File]::SetAttributes($taskMutated,[IO.FileAttributes]::ReadOnly)
        $taskHash=(Get-FileHash -LiteralPath $taskMutated).Hash;$r=Run-Guard @('--verify-game',('"'+$taskMutated+'"'))
        Check ($r.code -eq $taskExpected) ('bounded normalized qualification '+$taskCase)
        if($taskExpected -eq 10){Check ($r.output.Contains('Warning:')) 'metadata variant clearly warns but is allowed'}
        Check ((Get-FileHash -LiteralPath $taskMutated).Hash -eq $taskHash) 'verifier never rewrites private image'
    }
    foreach($key in $taskCopyBefore.Keys){Check ((Snapshot-File $taskCopy)[$key] -eq $taskCopyBefore[$key]) ('original private image '+$key+' unchanged')}
    $taskInstall=Join-Path $taskFixture ('mod '+$taskIndex);New-Item -ItemType Directory -Path $taskInstall|Out-Null
    $r=Run-Guard @('--save-game-selection',('"'+$taskInstall+'"'),('"'+$taskCopy+'"'))
    Check ($r.code -eq 0) 'local game selection saved'
    $taskSelection=[regex]::Match([IO.File]::ReadAllText((Join-Path $taskInstall 'launcher.ini')),'(?m)^GameExe=(.*)').Groups[1].Value.Trim()
    Check (-not [IO.Path]::IsPathRooted($taskSelection) -and [IO.Path]::GetFullPath((Join-Path $taskInstall $taskSelection)) -eq $taskCopy) 'same-volume local game path is relative'
    $r=Run-Guard @('--save-game-selection',('"'+$taskFolder+'"'),('"'+$taskCopy+'"'));Check ($r.code -ne 0) 'cannot use the game directory as mod target'
    Check (-not(Test-Path -LiteralPath (Join-Path $taskFolder 'launcher.ini'))) 'game folder remains untouched'
    $r=Run-Guard @('--save-auto-selection',('"'+$taskInstall+'"'),('"'+$taskCopy+'"'));Check ($r.code -eq 0) 'automatic game selection saved'
    Check ([regex]::IsMatch([IO.File]::ReadAllText((Join-Path $taskInstall 'launcher.ini')),'(?m)^GameExe=\s*$')) 'automatic Steam selection has no machine-local path'
    $taskIndex++
}
$r=Run-Guard @('--verify-game',('"'+(Join-Path $taskFixture 'missing.exe')+'"'));Check ($r.code -eq 5) 'missing image reported without writes'
foreach($taskSource in $taskSources.Keys){$now=Snapshot-File $taskSource;foreach($key in $taskSources[$taskSource].Keys){Check ($now[$key] -eq $taskSources[$taskSource][$key]) ('installed source never passed to executable and '+$key+' unchanged')}}
Write-Output ("Installer game verifier: $taskChecks checks passed; private readonly PE copies, no game or audio run.")
Write-Output ('Evidence: '+$taskFixture)
