param([string]$Collector=(Join-Path $PSScriptRoot '..\..\scripts\Collect-KL1-Diagnostics.ps1'))
$ErrorActionPreference='Stop'
$checks=0
function Check([bool]$Value,[string]$Message){$script:checks++;if(-not $Value){throw $Message}}
$root=Join-Path ([IO.Path]::GetTempPath()) ('kl1-diagnostics-test-'+[Guid]::NewGuid().ToString('N'))
$package=Join-Path $root 'package'
$exports=Join-Path $root 'exports'
[IO.Directory]::CreateDirectory($package) | Out-Null
$utf8=[Text.UTF8Encoding]::new($false)
function Put([string]$Relative,[string]$Text) {
    $path=Join-Path $package $Relative
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path)) | Out-Null
    [IO.File]::WriteAllText($path,$Text,$utf8)
    return $path
}
# Substitute the Windows-only query, not the collector's file/ZIP behavior.
# The recorded StartTime proves an AFTER-crash export looks into the past.
$global:Kl1TestEventFilters=[Collections.Generic.List[object]]::new()
function Event-Fixture([string]$Log,[int]$Id,[long]$Record,[string]$Provider,[string]$Message,[DateTime]$Created=[DateTime]::UtcNow.AddMinutes(-15)) {
    return [pscustomobject]@{LogName=$Log;TimeCreated=$Created;Id=$Id;RecordId=$Record;ProviderName=$Provider;Message=$Message}
}
$global:Kl1TestEvents=@(
    (Event-Fixture 'Application' 1000 1 'Application Error' 'Faulting application name: KANEANDLYNCH.EXE, faulting module: driver.dll' ([DateTime]::UtcNow.AddHours(-2))),
    (Event-Fixture 'Application' 1001 2 'Windows Error Reporting' 'KaneLynchOpenXRHost.exe stopped' ([DateTime]::UtcNow.AddHours(-1))),
    (Event-Fixture 'Application' 1000 3 'Application Error' 'unrelated.exe stopped'),
    (Event-Fixture 'Application' 1002 4 'Application Hang' 'The program kaneandlynch.exe stopped interacting with Windows'),
    (Event-Fixture 'System' 1001 11 'Microsoft-Windows-WER-SystemErrorReporting' 'The bugcheck was: 0x00000116 (0xffffd00f519ed010, 0xfffff80076e9be40, 0xffffffffc000009a, 4).'),
    (Event-Fixture 'System' 1019 12 'Microsoft-Windows-WER-SystemErrorReporting' 'Possibly related driver: nvlddmkm.sys.'),
    (Event-Fixture 'System' 4101 13 'Display' 'Display driver nvlddmkm stopped responding and has successfully recovered.'),
    (Event-Fixture 'System' 1001 14 'Microsoft-Windows-WER-SystemErrorReporting' 'The bugcheck was: 0x00000119 (1,2,3,4).'),
    (Event-Fixture 'System' 1001 15 'Microsoft-Windows-WER-SystemErrorReporting' 'The bugcheck was: 0x0000010e (1,2,3,4).'),
    (Event-Fixture 'System' 1001 16 'Microsoft-Windows-WER-SystemErrorReporting' 'Recovery failed in dxgkrnl.sys or dxgmms2.sys.'),
    (Event-Fixture 'System' 1001 17 'Microsoft-Windows-WER-SystemErrorReporting' 'The bugcheck was: 0x00000117 (1,2,3,4).'),
    (Event-Fixture 'System' 1001 18 'Microsoft-Windows-WER-SystemErrorReporting' 'The bugcheck was: 0x00000141 (1,2,3,4).'),
    (Event-Fixture 'Application' 1001 21 'Windows Error Reporting' "Event Name: LiveKernelEvent`nProblem signature:`nP1: 117`nP2: aaaaaaaa"),
    (Event-Fixture 'Application' 1001 22 'Windows Error Reporting' "Event Name: LiveKernelEvent`nP1: 141`nP2: aaaaaaaa"),
    (Event-Fixture 'System' 1001 31 'Microsoft-Windows-WER-SystemErrorReporting' 'The bugcheck was: 0x0000009f (1,2,3,4).'),
    (Event-Fixture 'System' 1001 32 'Another provider' 'The bugcheck was: 0x00000116 nvlddmkm.sys'),
    (Event-Fixture 'System' 4101 33 'Another provider' 'nvlddmkm stopped responding'),
    (Event-Fixture 'System' 41 34 'Microsoft-Windows-Kernel-Power' 'nvlddmkm.sys 0x116'),
    (Event-Fixture 'Application' 1001 35 'Windows Error Reporting' "Event Name: APPCRASH`nP1: 117`nP2: nvlddmkm.sys"),
    (Event-Fixture 'Application' 1001 36 'Windows Error Reporting' "Event Name: LiveKernelEvent`nP1: 114`nP2: 117"),
    (Event-Fixture 'Application' 1001 37 'Windows Error Reporting' "Event Name: LiveKernelEvent`nP1: 1170"),
    (Event-Fixture 'Application' 1001 38 'Another provider' "Event Name: LiveKernelEvent`nP1: 117"),
    (Event-Fixture 'Application' 1000 39 'Application Error' 'nvlddmkm.sys 0x117'),
    (Event-Fixture 'System' 1001 40 'Microsoft-Windows-WER-SystemErrorReporting' 'The bugcheck was: 0x00001160 (1,2,3,4).'),
    (Event-Fixture 'System' 1001 41 'Microsoft-Windows-WER-SystemErrorReporting' 'The bugcheck was: 0x116' ([DateTime]::UtcNow.AddHours(-50))),
    (Event-Fixture 'Application' 1001 42 'Windows Error Reporting' "Event Name: LiveKernelEvent`nP1: 141" ([DateTime]::UtcNow.AddHours(2))),
    (Event-Fixture 'Application' 1002 43 'Application Hang' 'kaneandlynch.exe' ([DateTime]::UtcNow.AddHours(-50))),
    (Event-Fixture 'Application' 1000 44 'Application Error' 'kaneandlynch.exe' ([DateTime]::UtcNow.AddHours(2)))
)
function global:Get-WinEvent {
    param($FilterHashtable,$MaxEvents,$ErrorAction)
    $global:Kl1TestEventFilters.Add(@{filter=$FilterHashtable;maximum=$MaxEvents})
    # Deliberately ignore provider/ID/window filtering so the collector's own
    # guards are exercised rather than merely trusting the mocked provider.
    $global:Kl1TestEvents | Where-Object {$_.LogName-eq $FilterHashtable.LogName}
}
function global:Start-Process {throw 'Collector must not start processes'}
function global:Stop-Process {throw 'Collector must not stop processes'}
function Export([hashtable]$Options=@{}) {
    $arguments=@{PackageDirectory=$package;OutputDirectory=$exports;Seconds=0;SessionCount=2;Label='fixture'}
    foreach($key in $Options.Keys){$arguments[$key]=$Options[$key]}
    $result=@(& $Collector @arguments)
    $path=[string]$result[-1]
    Check ([IO.File]::Exists($path)) 'ZIP export must exist'
    $expanded=Join-Path $root ('expanded-'+[Guid]::NewGuid().ToString('N'))
    [IO.Compression.ZipFile]::ExtractToDirectory($path,$expanded)
    return @{Archive=$path;Expanded=$expanded;Manifest=(Get-Content -LiteralPath (Join-Path $expanded 'collection.json') -Raw | ConvertFrom-Json)}
}
try {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $old='session-20261101T000000Z-77-0000000000000001'
    $first='session-20261102T000000Z-77-0000000000000002'
    $second='session-20261103T000000Z-77-0000000000000003'
    foreach($session in @($old,$first,$second)) {
        Put "logs/$session/manifest.json" ('{"gamePid":77,"identity":"'+$session+'"}') | Out-Null
        Put "logs/$session/crash.jsonl" ('{"source":"'+$session+'","error":"access-violation"}') | Out-Null
        Put "logs/$session/last-host.json" '{"source":"meta-host","pid":88}' | Out-Null
        Put "logs/$session/session.jsonl" 'render error source' | Out-Null
        Put "logs/$session/summary.json" '{"source":"openxr-host","pid":99}' | Out-Null
        Put "logs/$session/host.log" 'XR error source' | Out-Null
        Put "logs/$session/last-game.json" '{"pid":77,"captureEnabled":true}' | Out-Null
        Put "logs/$session/game.dmp" 'synthetic minidump fixture, no process memory' | Out-Null
    }
    $legacyException=Put 'logs/exceptions-42-100.jsonl' 'legacy exception without any performance file'
    Put 'logs/process-42-100.jsonl' 'legacy process exit' | Out-Null
    Put 'logs/memory-reserve-42-100.jsonl' 'legacy allocation failure' | Out-Null
    Put 'logs/launcher-20261109T182000-42-100-1.jsonl' '{"event":"launcher_stage","phase":"native_exited_before_mod","nativeExitCode":53}' | Out-Null
    Put 'vr.ini' "[Diagnostics]`nEnableLogging=1`n" | Out-Null
    Put 'KaneLynchVR.dll' 'fixture module hash' | Out-Null
    $source=Join-Path $package "logs/$first/crash.jsonl"
    $before=(Get-FileHash -LiteralPath $source).Hash
    $livePath=Put "logs/$second/live.log" 'writer remains open'
    $live=[IO.FileStream]::new($livePath,[IO.FileMode]::Open,[IO.FileAccess]::Write,[IO.FileShare]::ReadWrite)
    try {$one=Export} finally{$live.Dispose()}
    Check ($one.Manifest.sessions.Count-eq 2) 'latest requested session count selected'
    Check ($one.Manifest.sessions.directory-contains $first-and $one.Manifest.sessions.directory-contains $second) 'same PID with different creation identities remains two sessions'
    Check (-not [IO.Directory]::Exists((Join-Path $one.Expanded "logs/$old"))) 'older session omitted only from export'
    Check ([IO.Directory]::Exists((Join-Path $package "logs/$old"))) 'older source session must never be deleted'
    foreach($session in @($first,$second)) {
        Check ([IO.File]::Exists((Join-Path $one.Expanded "logs/$session/crash.jsonl"))) 'game crash source preserved'
        Check ([IO.File]::Exists((Join-Path $one.Expanded "logs/$session/session.jsonl"))) 'process observer source preserved'
        Check ([IO.File]::Exists((Join-Path $one.Expanded "logs/$session/host.log"))) 'host source preserved without inventing a parent identity'
        foreach($name in @('manifest.json','summary.json','last-game.json','last-host.json','game.dmp')){
            Check ([IO.File]::Exists((Join-Path $one.Expanded "logs/$session/$name"))) 'KL1 manifest, exit summary, snapshots and dump are preserved'
        }
    }
    foreach($name in @('exceptions-42-100.jsonl','process-42-100.jsonl','memory-reserve-42-100.jsonl','launcher-20261109T182000-42-100-1.jsonl')) {
        Check ([IO.File]::Exists((Join-Path $one.Expanded "legacy/$name"))) 'legacy crash/process/reserve files do not require performance logging'
    }
    Check ([IO.File]::Exists((Join-Path $one.Expanded "logs/$second/live.log"))) 'active append writer can remain open during snapshot'
    Check ((Get-FileHash -LiteralPath $source).Hash-eq $before) 'collection does not modify prior errors'
    $crashQuery=@($global:Kl1TestEventFilters | Where-Object {$_.filter.LogName-eq 'Application'-and $_.filter.Id-contains 1002})[0]
    Check ($crashQuery.filter.StartTime.ToUniversalTime()-lt [DateTime]::UtcNow.AddHours(-47)) 'WER query includes crashes before collector launch'
    Check ($crashQuery.filter.Id-contains 1000-and $crashQuery.filter.Id-contains 1001-and $crashQuery.filter.Id-contains 1002) 'WER query includes application error, reporting and hang IDs'
    $events=Get-Content -LiteralPath (Join-Path $one.Expanded 'crash-events.json') -Raw | ConvertFrom-Json
    Check ($events.Count-eq 3-and $events.recordId-contains 4-and $events.recordId-notcontains 3) 'only KL1 events included, including Application Hang1002'
    Check ($events.recordId-notcontains 43-and $events.recordId-notcontains 44) 'crash records outside explicit past/future window are rejected'
    $gpu=Get-Content -LiteralPath (Join-Path $one.Expanded 'gpu-events.json') -Raw | ConvertFrom-Json
    Check ($gpu.scope-eq 'machine-wide-context'-and $gpu.attribution-eq 'not-attributed-to-game'-and $gpu.note-match 'other applications') 'GPU events explicitly remain machine-wide context, never an attributed game failure'
    foreach($identity in @(11,12,13,14,15,16,17,18,21,22)){
        Check ($gpu.events.recordId-contains $identity) 'actual GPU bugcheck, suspect driver, display reset and explicit WER signatures are retained'
    }
    foreach($identity in @(3,31,32,33,34,35,36,37,38,39,40,41,42)){
        Check ($gpu.events.recordId-notcontains $identity) 'unrelated code/provider/ID/app/signature/window cannot masquerade as a GPU event'
    }
    Check ($gpu.events.Count-eq 10-and $gpu.maximumEvents-eq 256) 'only qualified GPU events are exported under the256-record limit'
    Check ($gpu.windowStartUtc-eq $one.Manifest.eventWindowStartUtc-and $gpu.windowEndUtc-eq $one.Manifest.eventWindowEndUtc) 'GPU and game events share one explicit UTC collection window'
    Check (@($global:Kl1TestEventFilters | Where-Object {$_.maximum-ne 4096-or $_.filter.StartTime.Kind-ne [DateTimeKind]::Local-or $_.filter.EndTime.Kind-ne [DateTimeKind]::Local}).Count-eq 0) 'every Windows query has bounded discovery and local DateTime bounds required by Windows PowerShell5'
    Check (@($global:Kl1TestEventFilters | Where-Object {$_.filter.StartTime.ToUniversalTime().ToString('o')-ne $one.Manifest.eventWindowStartUtc-or $_.filter.EndTime.ToUniversalTime().ToString('o')-ne $one.Manifest.eventWindowEndUtc}).Count-eq 0) 'native local query bounds represent the exact instants declared by UTC export metadata'
    $originalEvents=$global:Kl1TestEvents
    try {
        $now=[DateTime]::UtcNow.AddMinutes(-10)
        $global:Kl1TestEvents=@(
            (Event-Fixture 'System' 1001 51 'Microsoft-Windows-WER-SystemErrorReporting' 'The bugcheck was: 0x116' $now.ToLocalTime()),
            (Event-Fixture 'System' 1001 52 'Microsoft-Windows-WER-SystemErrorReporting' 'The bugcheck was: 0x116' ([DateTime]::UtcNow.AddHours(-2)))
        )
        $shortWindow=Export @{LookbackHours=1}
        $shortGpu=Get-Content -LiteralPath (Join-Path $shortWindow.Expanded 'gpu-events.json') -Raw | ConvertFrom-Json
        Check ($shortGpu.events.Count-eq 1-and $shortGpu.events[0].recordId-eq 51-and $shortGpu.events[0].utc-eq $now.ToString('o')) 'local event timestamps convert to UTC and caller lookback excludes older GPU events'
        $global:Kl1TestEvents=@(for($index=0;$index-lt 600;$index++){
            if($index%2){Event-Fixture 'Application' 1001 (1000+$index) 'Windows Error Reporting' "Event Name: LiveKernelEvent`nP1: 141" $now.AddMilliseconds(-$index)}
            else{Event-Fixture 'System' 1001 (1000+$index) 'Microsoft-Windows-WER-SystemErrorReporting' 'The bugcheck was: 0x00000116' $now.AddMilliseconds(-$index)}
        })
        $capped=Export
        $cappedGpu=Get-Content -LiteralPath (Join-Path $capped.Expanded 'gpu-events.json') -Raw | ConvertFrom-Json
        Check ($cappedGpu.events.Count-eq 256-and $cappedGpu.events[0].recordId-eq 1000-and $cappedGpu.events[-1].recordId-eq 1255) 'combined System/Application GPU export keeps exactly the newest256 records'
        Check (@($cappedGpu.events | Where-Object {$_.utc-notmatch 'Z$'-or [DateTime]::Parse($_.utc).ToUniversalTime()-lt [DateTime]::Parse($cappedGpu.windowStartUtc).ToUniversalTime()-or [DateTime]::Parse($_.utc).ToUniversalTime()-gt [DateTime]::Parse($cappedGpu.windowEndUtc).ToUniversalTime()}).Count-eq 0) 'every capped record is UTC and lies inside the declared window'
    } finally {$global:Kl1TestEvents=$originalEvents}
    $hashes=@(Get-Content -LiteralPath (Join-Path $one.Expanded 'package-hashes.json') -Raw | ConvertFrom-Json)
    Check ($hashes.Count-eq 1-and $hashes[0].file-eq 'KaneLynchVR.dll') 'runtime source hash recorded without copying executable'
    Check (-not [IO.File]::Exists((Join-Path $one.Expanded 'package/KaneLynchVR.dll'))) 'runtime binary is hashed rather than copied'
    Put 'logs/performance-run-a.jsonl' '{"event":"sample","sequence":2}' | Out-Null
    Put 'logs/performance-run-a.jsonl.1' '{"event":"sample","sequence":1}' | Out-Null
    $hostSession='session-20261101T120000Z-99-0000000000000099'
    Put "logs/$hostSession/crash.jsonl" '{"event":"exception","pid":99,"processCreationFt":153}' | Out-Null
    Put "logs/$hostSession/game.dmp" 'synthetic host dump' | Out-Null
    $withHost=Export @{SessionCount=3}
    Check ($withHost.Manifest.sessions.directory-contains $hostSession) 'independent host session is retained by creation identity'
    Check ([IO.File]::Exists((Join-Path $withHost.Expanded "logs/$hostSession/crash.jsonl"))) 'host exception does not require a game manifest or performance file'
    foreach($name in @('performance-run-a.jsonl','performance-run-a.jsonl.1')){
        Check ([IO.File]::Exists((Join-Path $withHost.Expanded "legacy/$name"))) 'selected legacy performance run retains its rotated parts'
    }
    $two=Export @{PackageDirectory=($package+[IO.Path]::DirectorySeparatorChar)}
    Check ($one.Archive-ne $two.Archive-and [IO.File]::Exists($one.Archive)) 'repeated exports never overwrite the previous ZIP'
    Check ($two.Manifest.sessions.Count-eq 2) 'package path with trailing separator preserves source discovery'
    Check ((Get-FileHash -LiteralPath $source).Hash-eq $before-and [IO.File]::Exists($legacyException)) 'second export preserves all original sources'
    $large=Join-Path $package "logs/$second/large.log"
    [IO.File]::WriteAllBytes($large,[byte[]]::new(1048577))
    Put "logs/$second/first-large.log" ('x'*900000) | Out-Null
    Put "logs/$second/second-large.log" ('y'*900000) | Out-Null
    $bounded=Export @{MaximumFileMiB=1;MaximumTotalMiB=1}
    Check ($bounded.Manifest.copiedBytes-le 1048576) 'total copied source bytes remain bounded'
    Check ($bounded.Manifest.skipped.reason-contains 'file-size-limit') 'oversized files explicitly reported rather than silently lost'
    Check ($bounded.Manifest.skipped.reason-contains 'total-size-limit') 'total-size exclusion explicitly reported'
    Check (-not [IO.File]::Exists((Join-Path $bounded.Expanded "logs/$second/large.log"))) 'oversized source not copied'
    Check ((Get-Item -LiteralPath $large).Length-eq 1048577) 'oversized original remains untouched'
    $counted=Export @{MaximumFiles=1}
    Check ($counted.Manifest.files.Count-eq 1-and $counted.Manifest.skipped.reason-contains 'file-count-limit') 'file-count cap is enforced and visible'
    Put 'KaneLynchVR-status.json' '{"pid":77,"updatedTick":123,"failure":"test"}' | Out-Null
    $sampled=Export @{Seconds=1}
    $sample=@(Get-Content -LiteralPath (Join-Path $sampled.Expanded 'samples.jsonl') | ConvertFrom-Json)
    Check ($sample.Count-eq 1-and $sample[0].'KaneLynchVR-status.json'.pid-eq 77) 'optional passive snapshot executes without starting a game'
    Check ($sampled.Manifest.passiveSecondsRequested-eq 1) 'passive sampling intent recorded separately from stored sessions'
    $rejected=$false
    try{Export @{OutputDirectory=(Join-Path $package 'logs/export')} | Out-Null}catch{$rejected=$true}
    Check $rejected 'export cannot recurse into its own recorded source tree'
    $outside=Join-Path $root 'outside-secret.log';[IO.File]::WriteAllText($outside,'outside canary')
    $link=Join-Path $package "logs/$second/link.log"
    try {
        New-Item -ItemType SymbolicLink -Path $link -Target $outside -ErrorAction Stop | Out-Null
        $linked=Export
        Check (-not [IO.File]::Exists((Join-Path $linked.Expanded "logs/$second/link.log"))) 'links outside session root are not followed'
        Check ($linked.Manifest.skipped.reason-contains 'linked-or-outside-package') 'link omission is recorded'
        Check ([IO.File]::ReadAllText($outside)-eq 'outside canary') 'external canary remains untouched'
    } catch [System.UnauthorizedAccessException] {Write-Host 'Symbolic-link fixture unavailable on this Windows account.'}
    function global:Get-WinEvent {param($FilterHashtable,$MaxEvents,$ErrorAction);throw 'fixture: Windows event provider unavailable'}
    $noEvents=Export
    Check (@($noEvents.Manifest.notes | Where-Object {$_-match 'Windows crash event log unavailable'}).Count-eq 1) 'missing Windows event provider still produces an export with an explicit note'
    $emptyGpu=Get-Content -LiteralPath (Join-Path $noEvents.Expanded 'gpu-events.json') -Raw | ConvertFrom-Json
    Check ($emptyGpu.events.Count-eq 0-and @($noEvents.Manifest.notes | Where-Object {$_-match 'GPU event log unavailable'}).Count-eq 2) 'unavailable System/Application GPU logs produce an empty contextual report and separate notes'
    for($i=0;$i-lt 180;$i++){Put "logs/$second/extra-$i.log" 'bounded metadata fixture' | Out-Null}
    $many=Export @{MaximumFiles=1}
    Check ($many.Manifest.skipped.Count-le 128-and $many.Manifest.omissionCounts.'file-count-limit'-gt 128) 'omission details remain bounded while aggregate counts preserve additional failures'
    $scanned=Export @{MaximumFiles=1;MaximumScanEntries=32}
    Check ($scanned.Manifest.scannedSessionEntries-le 32-and $scanned.Manifest.scanLimitReached) 'content discovery stops at a bounded filesystem-entry count'
    Check (@($scanned.Manifest.notes | Where-Object {$_-match 'entry limit|directory/entry limit'}).Count-ge 1) 'bounded discovery omissions are explicitly reported'
    $global:Kl1FailCopyHash=$true;$global:Kl1FailedCopyRelative=''
    function global:Get-FileHash {
        param($LiteralPath,$Algorithm='SHA256')
        if($global:Kl1FailCopyHash-and $LiteralPath.StartsWith($exports)-and $LiteralPath-match '[\\/](logs[\\/]session-.*host\.log)$'){
            $global:Kl1FailCopyHash=$false;$global:Kl1FailedCopyRelative=$Matches[1].Replace('\','/')
            throw 'fixture: failure after destination copy'
        }
        Microsoft.PowerShell.Utility\Get-FileHash -LiteralPath $LiteralPath -Algorithm $Algorithm
    }
    try{$failed=Export}finally{Remove-Item Function:\Get-FileHash -ErrorAction SilentlyContinue}
    Check (-not $global:Kl1FailCopyHash-and $failed.Manifest.skipped.reason-contains 'unavailable-during-copy') 'a copy/hash failure remains visible without aborting other sources'
    Check (-not [IO.File]::Exists((Join-Path $failed.Expanded $global:Kl1FailedCopyRelative))) 'failed own destination is removed instead of adding an unlisted partial to ZIP'
    Check ([IO.File]::Exists((Join-Path $package $global:Kl1FailedCopyRelative))) 'copy failure never removes the recorded source'
    $linkedPackage=Join-Path $root 'linked-package'
    try {
        New-Item -ItemType SymbolicLink -Path $linkedPackage -Target $package -ErrorAction Stop | Out-Null
        $rejected=$false
        try {& $Collector -PackageDirectory $linkedPackage -OutputDirectory $exports -Seconds 0 | Out-Null}catch{$rejected=$true}
        Check $rejected 'linked package root is rejected before following outside source entries'
    } catch [System.UnauthorizedAccessException] {Write-Host 'Root symbolic-link fixture unavailable on this Windows account.'}
    # Substitute ONLY discovery of Windows LocalAppData. All identity parsing,
    # path/reparse checks, source handles, budgets and ZIP copies remain the
    # actual collector. Never create/delete anything in the user's CrashDumps.
    $originalPackage=$package;$originalCollector=$Collector
    $package=Join-Path $root 'wer-package';[IO.Directory]::CreateDirectory($package) | Out-Null
    $werAppData=Join-Path $root 'wer-local-appdata';$werDumpRoot=Join-Path $werAppData 'CrashDumps'
    [IO.Directory]::CreateDirectory($werDumpRoot) | Out-Null
    $collectorText=[IO.File]::ReadAllText($Collector)
    $discovery='$localAppData=[Environment]::GetFolderPath([Environment+SpecialFolder]::LocalApplicationData)'
    Check (($collectorText.Split(@($discovery),[StringSplitOptions]::None)).Count-eq 2) 'fixture replaces exactly one Windows LocalAppData discovery expression'
    $Collector=Join-Path $root 'collector-with-fixture-localappdata.ps1'
    [IO.File]::WriteAllText($Collector,$collectorText.Replace($discovery,('$localAppData='''+$werAppData.Replace("'","''")+"'")),$utf8)
    [uint32]$werProcessId=884411
    $werDate=[DateTime]::Parse('2026-11-09T20:01:00.9876543Z').ToUniversalTime()
    [uint64]$werCreated=$werDate.ToFileTimeUtc()
    [uint32]$werSeconds=[Math]::Floor(([decimal]$werCreated-116444736000000000)/10000000)
    $werSession='session-20261109T200100Z-'+$werProcessId+'-'+$werCreated.ToString('x16')
    $werOlderCreated=$werCreated-100000000
    $werOlder='session-20261109T200050Z-'+$werProcessId+'-'+$werOlderCreated.ToString('x16')
    foreach($identity in @(@{name=$werSession;created=$werCreated},@{name=$werOlder;created=$werOlderCreated})){
        Put ('logs/'+$identity.name+'/manifest.json') (@{gamePid=$werProcessId;processCreationFileTime=$identity.created;gameExe='C:\Fixture\Games\Kane and Lynch Dead Men\kaneandlynch.exe'} | ConvertTo-Json -Compress) | Out-Null
    }
    $werDump=Join-Path $werDumpRoot ('kaneandlynch.exe.'+$werProcessId+'.dmp')
    function Write-WerFixture([hashtable]$Changes=@{}) {
        $fields=@{length=128;signature=[uint32]0x504D444D;version=[uint32]0xA793;count=[uint32]1;directory=[uint32]32;
            streamType=[uint32]15;miscBytes=[uint32]24;miscRva=[uint32]44;miscSize=[uint32]24;flags=[uint32]3;processId=$werProcessId;created=$werSeconds}
        foreach($key in $Changes.Keys){$fields[$key]=$Changes[$key]}
        $data=[byte[]]::new($fields.length)
        foreach($pair in @(@(0,$fields.signature),@(4,$fields.version),@(8,$fields.count),@(12,$fields.directory),
            @(32,$fields.streamType),@(36,$fields.miscBytes),@(40,$fields.miscRva),@(44,$fields.miscSize),@(48,$fields.flags),@(52,$fields.processId),@(56,$fields.created))){
            if($pair[0]+4-le $data.Length){[Array]::Copy([BitConverter]::GetBytes([uint32]$pair[1]),0,$data,$pair[0],4)}
        }
        [IO.File]::WriteAllBytes($werDump,$data)
    }
    try {
        Write-WerFixture
        $werBefore=(Get-FileHash -LiteralPath $werDump).Hash
        $valid=Export
        $fallback=@($valid.Manifest.files | Where-Object {$_.provenance.origin-eq 'WindowsWER.LocalDumps'})
        Check ($fallback.Count-eq 1-and [IO.File]::Exists((Join-Path $valid.Expanded "logs/$werSession/windows-wer.dmp"))) 'exact selected game session obtains one WER fallback when package dump is absent'
        Check (-not [IO.File]::Exists((Join-Path $valid.Expanded "logs/$werOlder/windows-wer.dmp"))) 'same reused PID with different creation does not receive the newer dump'
        Check ($fallback[0].provenance.processId-eq $werProcessId-and $fallback[0].provenance.processCreationFileTime-eq [string]$werCreated-and
            $fallback[0].provenance.minidumpProcessCreateTimeUnixSeconds-eq $werSeconds) 'manifest records precise session FILETIME and dump creation seconds provenance'
        Check ($fallback[0].bytesAtOpen-eq 128-and $fallback[0].copiedBytes-eq 128-and $fallback[0].sha256-eq $werBefore.ToLowerInvariant()) 'verified source handle is rewound and exact dump bytes are copied'
        Check ((Get-FileHash -LiteralPath $werDump).Hash-eq $werBefore) 'user-style WER source remains untouched after collection'
        $werManifestPath=Join-Path $package "logs/$werSession/manifest.json"
        $werManifestText=[IO.File]::ReadAllText($werManifestPath)
        foreach($wrongManifest in @(@{gamePid=$werProcessId+1;processCreationFileTime=$werCreated;gameExe='D:\kaneandlynch.exe'},
            @{gamePid=$werProcessId;processCreationFileTime=$werCreated+1;gameExe='D:\kaneandlynch.exe'},
            @{gamePid=$werProcessId;processCreationFileTime=$werCreated;gameExe='D:\KaneLynchVRHost.exe'})){
            [IO.File]::WriteAllText($werManifestPath,($wrongManifest | ConvertTo-Json -Compress),$utf8)
            $mismatch=Export @{SessionCount=1}
            Check (@($mismatch.Manifest.files | Where-Object {$_.provenance}).Count-eq 0) 'game executable and exact PID/FILETIME must agree between manifest and selected directory before external access'
        }
        [IO.File]::WriteAllText($werManifestPath,$werManifestText,$utf8)
        Write-WerFixture @{miscBytes=1364;miscSize=1364;length=2048}
        $extended=Export @{SessionCount=1}
        Check (@($extended.Manifest.files | Where-Object {$_.provenance}).Count-eq 1) 'larger Windows MISC_INFO versions retain the same verified PID/creation prefix'
        Write-WerFixture
        Put "logs/$werSession/game.dmp" 'existing package dump takes priority' | Out-Null
        $existing=Export @{SessionCount=1}
        Check (@($existing.Manifest.files | Where-Object {$_.provenance}).Count-eq 0-and [IO.File]::Exists((Join-Path $existing.Expanded "logs/$werSession/game.dmp"))) 'package-owned dump prevents duplicate WER inclusion'
        Remove-Item -LiteralPath (Join-Path $package "logs/$werSession/game.dmp") -Force
        foreach($bad in @(@{processId=$werProcessId+1},@{created=$werSeconds-1},@{created=$werSeconds+1},@{flags=0},@{flags=1},@{flags=2},
            @{length=20},@{signature=0},@{version=0},@{count=0},@{count=[uint32]::MaxValue},@{directory=[uint32]::MaxValue},
            @{streamType=3},@{miscRva=[uint32]::MaxValue},@{miscBytes=[uint32]::MaxValue},@{miscSize=20},@{miscSize=28})){
            Write-WerFixture $bad;$invalid=Export @{SessionCount=1}
            Check (@($invalid.Manifest.files | Where-Object {$_.provenance}).Count-eq 0-and
                $invalid.Manifest.skipped.reason-contains 'wer-dump-identity-rejected') 'foreign/reused identity or invalid bounded minidump header is rejected before copying'
            Check ([IO.File]::Exists($werDump)) 'rejected dump source is preserved'
        }
        Write-WerFixture
        $wrongName=Join-Path $werDumpRoot ('unrelated.exe.'+$werProcessId+'.dmp')
        Move-Item -LiteralPath $werDump -Destination $wrongName
        $wrong=Export @{SessionCount=1}
        Check (@($wrong.Manifest.files | Where-Object {$_.provenance}).Count-eq 0) 'collector never searches other executable names in CrashDumps'
        Move-Item -LiteralPath $wrongName -Destination $werDump
        $ambiguousCreated=$werCreated+1
        $ambiguousSession='session-20261109T200101Z-'+$werProcessId+'-'+$ambiguousCreated.ToString('x16')
        Put "logs/$ambiguousSession/manifest.json" (@{gamePid=$werProcessId;processCreationFileTime=$ambiguousCreated;gameExe='D:\kaneandlynch.exe'} | ConvertTo-Json -Compress) | Out-Null
        $ambiguous=Export
        Check (@($ambiguous.Manifest.files | Where-Object {$_.provenance}).Count-eq 0-and
            @($ambiguous.Manifest.notes | Where-Object {$_-match 'ambiguous PID/second'}).Count-gt 0) 'two selected PID creations in the same second are rejected instead of guessing which dump belongs to them'
        Put "logs/$ambiguousSession/manifest.json" '{"gamePid":1,"gameExe":"D:\\KaneLynchVRHost.exe","processCreationFileTime":1}' | Out-Null
        Write-WerFixture @{length=1048577}
        $werSize=Export @{SessionCount=2;MaximumFileMiB=1}
        Check (@($werSize.Manifest.files | Where-Object {$_.provenance}).Count-eq 0-and $werSize.Manifest.skipped.reason-contains 'file-size-limit') 'WER fallback obeys the existing per-file byte budget'
        Write-WerFixture @{length=900000};Put "logs/$werSession/budget.log" ('x'*900000) | Out-Null
        $werTotal=Export @{SessionCount=2;MaximumTotalMiB=1}
        Check ($werTotal.Manifest.copiedBytes-le 1048576-and @($werTotal.Manifest.files | Where-Object {$_.provenance}).Count-eq 0-and
            $werTotal.Manifest.skipped.reason-contains 'total-size-limit') 'WER fallback cannot bypass aggregate byte budget'
        $werCount=Export @{SessionCount=2;MaximumFiles=1}
        Check ($werCount.Manifest.files.Count-eq 1-and @($werCount.Manifest.files | Where-Object {$_.provenance}).Count-eq 0-and
            $werCount.Manifest.skipped.reason-contains 'file-count-limit') 'WER fallback cannot bypass file-count budget'
        $junctionData=Join-Path $root 'wer-junction-appdata';[IO.Directory]::CreateDirectory($junctionData) | Out-Null
        $junction=Join-Path $junctionData 'CrashDumps'
        try {
            New-Item -ItemType Junction -Path $junction -Target $werDumpRoot -ErrorAction Stop | Out-Null
            [IO.File]::WriteAllText($Collector,$collectorText.Replace($discovery,('$localAppData='''+$junctionData.Replace("'","''")+"'")),$utf8)
            $werLinked=Export @{SessionCount=2}
            Check (@($werLinked.Manifest.files | Where-Object {$_.provenance}).Count-eq 0-and $werLinked.Manifest.skipped.reason-contains 'linked-or-outside-wer-root') 'canonical CrashDumps root cannot be a junction/reparse path'
            Check ([IO.File]::Exists($werDump)) 'linked-root rejection never deletes the original dump'
        } catch [System.UnauthorizedAccessException] {Write-Host 'WER junction fixture unavailable on this Windows account.'}
    } finally {$package=$originalPackage;$Collector=$originalCollector}
    Write-Host "KL1 diagnostics collector: $checks checks PASS; real filesystem and ZIP, WER provider substituted; no game/runtime started"
} finally {
    Remove-Item Function:\Get-WinEvent,Function:\Start-Process,Function:\Stop-Process -ErrorAction SilentlyContinue
    # The complete fixture was created by this script under one fresh GUID.
    $resolvedFixture=[IO.Path]::GetFullPath($root)
    $tempRoot=[IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd([IO.Path]::DirectorySeparatorChar)+[IO.Path]::DirectorySeparatorChar
    if(-not $resolvedFixture.StartsWith($tempRoot,[StringComparison]::OrdinalIgnoreCase)-or
       [IO.Path]::GetFileName($resolvedFixture)-notmatch '^kl1-diagnostics-test-[0-9a-f]{32}$'){throw 'Fixture cleanup path escaped its fresh temporary root.'}
    if([IO.Directory]::Exists($resolvedFixture)){Remove-Item -LiteralPath $resolvedFixture -Recurse -Force}
}
