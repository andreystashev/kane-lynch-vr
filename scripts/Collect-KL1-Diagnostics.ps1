<#
.SYNOPSIS
Exports recent KL1 diagnostic process sessions, including after a crash or restart.
.DESCRIPTION
Does not start or stop the game and never removes recorded sessions. The default
is an immediate ZIP export. -Seconds enables optional passive status sampling.
#>
param(
    [string]$PackageDirectory = $PSScriptRoot,
    [string]$OutputDirectory = '',
    [ValidateRange(0,3600)][int]$Seconds = 0,
    [ValidateRange(1,100)][int]$SessionCount = 5,
    [ValidateRange(1,2160)][int]$LookbackHours = 48,
    [ValidateRange(1,256)][int]$MaximumFileMiB = 32,
    [ValidateRange(1,2048)][int]$MaximumTotalMiB = 512,
    [ValidateRange(1,4096)][int]$MaximumFiles = 512,
    [ValidateRange(16,65536)][int]$MaximumScanEntries = 8192,
    [ValidatePattern('^[\w-]{1,64}$')][string]$Label = 'support'
)
$ErrorActionPreference = 'Stop'
$package = [IO.Path]::GetFullPath($PackageDirectory)
if(-not [string]::Equals($package,[IO.Path]::GetPathRoot($package),[StringComparison]::OrdinalIgnoreCase)){
    $package=$package.TrimEnd([IO.Path]::DirectorySeparatorChar,[IO.Path]::AltDirectorySeparatorChar)
}
if(-not [IO.Directory]::Exists($package)){throw 'PackageDirectory must be an existing package folder.'}
if(((Get-Item -LiteralPath $package -Force).Attributes-band [IO.FileAttributes]::ReparsePoint)-ne 0){throw 'PackageDirectory must not be a linked/reparse folder.'}
$started = [DateTime]::UtcNow
$eventStart = $started.AddHours(-$LookbackHours)
if([string]::IsNullOrWhiteSpace($OutputDirectory)){$OutputDirectory=Join-Path $package 'diagnostics'}
$exportRoot=[IO.Path]::GetFullPath($OutputDirectory)
$sourceLogRoot=[IO.Path]::GetFullPath((Join-Path $package 'logs')).TrimEnd([IO.Path]::DirectorySeparatorChar)
if([string]::Equals($exportRoot,$sourceLogRoot,[StringComparison]::OrdinalIgnoreCase)-or
   $exportRoot.StartsWith($sourceLogRoot+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){
    throw 'OutputDirectory must be separate from the recorded logs folder.'
}
[IO.Directory]::CreateDirectory($exportRoot) | Out-Null
$exportName='KL1-diagnostics-'+$started.ToString('yyyyMMdd-HHmmss-fff')+'-'+$PID+'-'+[Guid]::NewGuid().ToString('N')+'-'+$Label
$output=Join-Path $exportRoot $exportName
$archive=$output+'.zip'
if([IO.Directory]::Exists($output)-or [IO.File]::Exists($archive)){throw 'Choose a new diagnostics output path.'}
[IO.Directory]::CreateDirectory($output) | Out-Null
$maximumFileBytes=[long]$MaximumFileMiB*1024*1024
$maximumTotalBytes=[long]$MaximumTotalMiB*1024*1024
$state=@{ bytes=[long]0; files=0; scanned=0; scanLimitReached=$false; enumerationLimitReached=$false }
$included=[Collections.Generic.List[object]]::new()
$skipped=[Collections.Generic.List[object]]::new()
$sessions=[Collections.Generic.List[object]]::new()
$notes=[Collections.Generic.List[string]]::new()
$seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$omissionCounts=@{}
$utf8=[Text.UTF8Encoding]::new($false)
$localAppData=[Environment]::GetFolderPath([Environment+SpecialFolder]::LocalApplicationData)
$werRoot=$null
if(-not [string]::IsNullOrWhiteSpace($localAppData)){
    $werRoot=[IO.Path]::GetFullPath((Join-Path $localAppData 'CrashDumps')).TrimEnd([IO.Path]::DirectorySeparatorChar)
}
$werCandidates=[Collections.Generic.List[object]]::new()

function Relative-PackagePath([string]$Path) {
    $full=[IO.Path]::GetFullPath($Path)
    $prefix=$package.TrimEnd([IO.Path]::DirectorySeparatorChar,[IO.Path]::AltDirectorySeparatorChar)+[IO.Path]::DirectorySeparatorChar
    if(-not $full.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)){return $null}
    return $full.Substring($prefix.Length).Replace('\','/')
}
function Safe-PackagePath([string]$Path) {
    $relative=Relative-PackagePath $Path
    if(-not $relative){return $false}
    $at=[IO.Path]::GetFullPath($Path)
    while(-not [string]::Equals($at,$package,[StringComparison]::OrdinalIgnoreCase)) {
        try{$item=Get-Item -LiteralPath $at -Force -ErrorAction Stop}catch{return $false}
        if(($item.Attributes-band [IO.FileAttributes]::ReparsePoint)-ne 0){return $false}
        $at=[IO.Path]::GetDirectoryName($at)
        if([string]::IsNullOrEmpty($at)){return $false}
    }
    return $true
}
function Diagnostic-Name([string]$Name) {
    return $Name-match '(?i)\.(?:json|jsonl|log|txt|dmp)(?:\.[1-7])?$'
}
function Event-InWindow($Event,[string]$LogName,[DateTime]$End) {
    try {
        if($Event.LogName-and $Event.LogName-ne $LogName){return $false}
        if(-not $Event.TimeCreated){return $false}
        $utc=$Event.TimeCreated.ToUniversalTime()
        return $utc-ge $eventStart-and $utc-le $End
    } catch {return $false}
}
function GPU-Event($Event,[string]$LogName) {
    $message=[string]$Event.Message
    if($LogName-eq 'System') {
        if($Event.Id-eq 4101-and $Event.ProviderName-eq 'Display'){return $true}
        return $Event.Id-in @(1001,1019)-and $Event.ProviderName-eq 'Microsoft-Windows-WER-SystemErrorReporting'-and
            $message-match '(?i)\b(?:0x0*(?:116|117|119|141|10e)|nvlddmkm(?:\.sys)?|dxgkrnl(?:\.sys)?|dxgmms\d*(?:\.sys)?)\b'
    }
    # Application WER has many unrelated crash reports. Require both the
    # LiveKernelEvent name and its explicit GPU signature, never a loose117
    # elsewhere in the message (an address, report ID or another app error).
    return $LogName-eq 'Application'-and $Event.Id-eq 1001-and $Event.ProviderName-eq 'Windows Error Reporting'-and
        $message-match '(?i)\bLiveKernelEvent\b'-and $message-match '(?i)\bP1\s*:\s*(?:0x)?0*(?:117|141)\b'
}
function Event-Record($Event,[string]$LogName) {
    $message=[string]$Event.Message
    return [ordered]@{ utc=$Event.TimeCreated.ToUniversalTime().ToString('o'); log=$LogName;
        id=$Event.Id; recordId=$Event.RecordId; provider=$Event.ProviderName;
        message=$message.Substring(0,[Math]::Min(16384,$message.Length)) }
}
function Skip-Diagnostic($Entry) {
    $reason=[string]$Entry.reason
    if(-not $omissionCounts.ContainsKey($reason)){$omissionCounts[$reason]=0}
    $omissionCounts[$reason]++
    if($skipped.Count-lt 128){$skipped.Add($Entry)}
}
function Scan-Entries([string]$Path,[int]$Limit) {
    # Use the filesystem iterator directly: Get-ChildItem can build/sort a
    # complete provider result before downstream Select-Object applies a cap.
    $iterator=[IO.Directory]::EnumerateFileSystemEntries($Path).GetEnumerator()
    $count=0
    try {
        while($iterator.MoveNext()) {
            if($count-ge $Limit){$state.enumerationLimitReached=$true;break}
            $count++
            try{Get-Item -LiteralPath $iterator.Current -Force -ErrorAction Stop}catch{}
        }
    } finally{$iterator.Dispose()}
}
function Safe-WerPath([string]$Path,[uint32]$ProcessIdentity) {
    if(-not $werRoot){return $false}
    $full=[IO.Path]::GetFullPath($Path)
    if(-not [string]::Equals([IO.Path]::GetDirectoryName($full),$werRoot,[StringComparison]::OrdinalIgnoreCase)-or
       -not [string]::Equals([IO.Path]::GetFileName($full),('kaneandlynch.exe.'+$ProcessIdentity+'.dmp'),[StringComparison]::OrdinalIgnoreCase)){return $false}
    # Check every component, including LocalAppData/CrashDumps itself. Never
    # follow a junction/symlink into another application's dump directory.
    $at=$full
    while($at) {
        try{$item=Get-Item -LiteralPath $at -Force -ErrorAction Stop}catch{return $false}
        if(($item.Attributes-band [IO.FileAttributes]::ReparsePoint)-ne 0){return $false}
        $parent=[IO.Path]::GetDirectoryName($at)
        if([string]::Equals($parent,$at,[StringComparison]::OrdinalIgnoreCase)){break}
        $at=$parent
    }
    return $true
}
function Read-DumpBytes([IO.FileStream]$Stream,[long]$Offset,[int]$Count) {
    if($Offset-lt 0-or $Count-lt 0-or $Offset-gt $Stream.Length-or $Count-gt $Stream.Length-$Offset){throw 'minidump range exceeds file'}
    $Stream.Position=$Offset;$bytes=[byte[]]::new($Count);$at=0
    while($at-lt $Count){$read=$Stream.Read($bytes,$at,$Count-$at);if($read-eq 0){throw 'minidump truncated during identity read'};$at+=$read}
    return ,$bytes
}
function Read-WerIdentity([IO.FileStream]$Stream,$Expected) {
    # MINIDUMP_HEADER + at most4096 directory entries + the first24 bytes of
    # exactly one MiscInfoStream. No dump payload or process memory is parsed.
    $header=Read-DumpBytes $Stream 0 32
    if([BitConverter]::ToUInt32($header,0)-ne 0x504D444D-or
       ([BitConverter]::ToUInt32($header,4)-band 0xFFFF)-ne 0xA793){throw 'invalid minidump signature/version'}
    $count=[BitConverter]::ToUInt32($header,8);$directory=[BitConverter]::ToUInt32($header,12)
    if($count-lt 1-or $count-gt 4096-or $directory-lt 32){throw 'minidump directory limit/range rejected'}
    $entries=Read-DumpBytes $Stream $directory ([int]$count*12)
    $miscRva=[long]-1;$miscBytes=[uint32]0
    for($index=0;$index-lt $count;$index++) {
        $at=$index*12
        if([BitConverter]::ToUInt32($entries,$at)-ne 15){continue}
        if($miscRva-ge 0){throw 'ambiguous duplicate minidump MiscInfoStream'}
        $miscBytes=[BitConverter]::ToUInt32($entries,$at+4);$miscRva=[BitConverter]::ToUInt32($entries,$at+8)
    }
    if($miscRva-lt 32-or $miscBytes-lt 24-or $miscBytes-gt 65536-or
       $miscRva-gt $Stream.Length-or $miscBytes-gt $Stream.Length-$miscRva){throw 'missing/truncated minidump MiscInfoStream'}
    $misc=Read-DumpBytes $Stream $miscRva 24
    $size=[BitConverter]::ToUInt32($misc,0);$flags=[BitConverter]::ToUInt32($misc,4)
    if($size-lt 24-or $size-gt $miscBytes-or ($flags-band 3)-ne 3){throw 'minidump PID/creation validity flags missing'}
    $dumpProcessId=[BitConverter]::ToUInt32($misc,8);$createdSeconds=[BitConverter]::ToUInt32($misc,12)
    if($dumpProcessId-ne $Expected.processId-or $createdSeconds-ne $Expected.createUnixSeconds){throw 'minidump PID/process creation does not match selected session'}
    return [ordered]@{ origin='WindowsWER.LocalDumps'; sourceRoot='LocalApplicationData/CrashDumps';
        session=$Expected.session; processId=$dumpProcessId; processCreationFileTime=[string]$Expected.creationFileTime;
        minidumpProcessCreateTimeUnixSeconds=$createdSeconds; miscInfoFlags=$flags }
}
function Read-GameSessionIdentity([IO.DirectoryInfo]$Directory) {
    if($Directory.Name-notmatch '^session-\d{8}T\d{6}Z-(\d+)-([0-9a-f]{16})$'){return $null}
    try {
        $sessionProcessId=[uint32]::Parse($Matches[1]);$created=[Convert]::ToUInt64($Matches[2],16)
        if(-not $sessionProcessId-or $created-lt [uint64]116444736000000000){return $null}
        $manifest=Read-StatusSnapshot (Join-Path $Directory.FullName 'manifest.json')
        if([string]$manifest.gamePid-notmatch '^\d+$'-or [string]$manifest.processCreationFileTime-notmatch '^\d+$'-or
           [uint32]$manifest.gamePid-ne $sessionProcessId-or [uint64]$manifest.processCreationFileTime-ne $created-or
           -not [IO.Path]::IsPathRooted([string]$manifest.gameExe)-or
           -not [string]::Equals([IO.Path]::GetFileName([string]$manifest.gameExe),'kaneandlynch.exe',[StringComparison]::OrdinalIgnoreCase)){return $null}
        $seconds=[Math]::Floor(([decimal]$created-[decimal]116444736000000000)/10000000)
        if($seconds-gt [uint32]::MaxValue){return $null}
        return @{ session=$Directory.Name; processId=$sessionProcessId; creationFileTime=$created; createUnixSeconds=[uint32]$seconds }
    } catch {return $null}
}
function Copy-Diagnostic([IO.FileInfo]$File,[string]$Destination,$WerSession=$null) {
    $relative=Relative-PackagePath $File.FullName
    if($WerSession) {
        $relative='windows-wer/CrashDumps/'+$File.Name
        if(-not (Safe-WerPath $File.FullName $WerSession.processId)){
            Skip-Diagnostic ([ordered]@{ source=$relative; reason='linked-or-outside-wer-root' });return
        }
    } elseif(-not $relative-or -not (Safe-PackagePath $File.FullName)){
        Skip-Diagnostic ([ordered]@{ source=$relative; reason='linked-or-outside-package' });return
    }
    $seenKey=$relative;if($WerSession){$seenKey=$WerSession.session+'/'+$relative}
    if(-not $seen.Add($seenKey)){return}
    if($state.files-ge $MaximumFiles){Skip-Diagnostic ([ordered]@{ source=$relative; reason='file-count-limit' });return}
    $sourceStream=$null;$target=$null;$destinationPath=$null;$destinationCreated=$false;$copyFailureReason='unavailable-during-copy'
    try {
        # Snapshot the length after opening. Read only those bytes even if the
        # active process continues appending; allow its existing write handle.
        $sourceStream=[IO.FileStream]::new($File.FullName,[IO.FileMode]::Open,[IO.FileAccess]::Read,
            [IO.FileShare]::ReadWrite-bor [IO.FileShare]::Delete)
        $length=$sourceStream.Length
        if($length-gt $maximumFileBytes){Skip-Diagnostic ([ordered]@{ source=$relative; reason='file-size-limit'; bytes=$length });return}
        if($state.bytes+$length-gt $maximumTotalBytes){Skip-Diagnostic ([ordered]@{ source=$relative; reason='total-size-limit'; bytes=$length });return}
        $provenance=$null
        if($WerSession) {
            $copyFailureReason='wer-dump-identity-rejected'
            if(-not (Safe-WerPath $File.FullName $WerSession.processId)){throw 'WER path changed during open'}
            # Identity is verified on the SAME read-only handle used to copy.
            $provenance=Read-WerIdentity $sourceStream $WerSession
            if($sourceStream.Length-ne $length){throw 'WER dump length changed during identity read'}
            $sourceStream.Position=0;$copyFailureReason='unavailable-during-copy'
        }
        $destinationPath=Join-Path $output $Destination
        [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destinationPath)) | Out-Null
        $target=[IO.FileStream]::new($destinationPath,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
        $destinationCreated=$true
        $buffer=[byte[]]::new(65536);$remaining=$length;$copied=[long]0
        while($remaining-gt 0) {
            $read=$sourceStream.Read($buffer,0,[int][Math]::Min($buffer.Length,$remaining))
            if($read-eq 0){break}
            $target.Write($buffer,0,$read);$remaining-=$read;$copied+=$read
        }
        $target.Dispose();$target=$null
        if($WerSession-and ($copied-ne $length-or $sourceStream.Length-ne $length)){throw 'WER dump changed/truncated during snapshot'}
        $hash=(Get-FileHash -LiteralPath $destinationPath -Algorithm SHA256).Hash.ToLowerInvariant()
        $state.bytes+=$copied;$state.files++
        $entry=[ordered]@{ source=$relative; file=$Destination.Replace('\','/'); bytesAtOpen=$length;
            copiedBytes=$copied; shortRead=($copied-ne $length); sha256=$hash }
        if($provenance){$entry.provenance=$provenance};$included.Add($entry)
    } catch {
        Skip-Diagnostic ([ordered]@{ source=$relative; reason=$copyFailureReason; detail=$_.Exception.Message })
        if($target){try{$target.Dispose()}catch{};$target=$null}
        # A failed destination was created only in this new collector-owned
        # staging tree. Never leave an unlisted partial file in the ZIP.
        if($destinationCreated-and [IO.File]::Exists($destinationPath)){
            # A cleanup failure must not silently add an unlisted partial to
            # the ZIP or bypass its copy budget. Leave this staging for review.
            Remove-Item -LiteralPath $destinationPath -Force -ErrorAction Stop
        }
    } finally {
        if($target){try{$target.Dispose()}catch{}};if($sourceStream){$sourceStream.Dispose()}
    }
}
function Read-StatusSnapshot([string]$Path) {
    if(-not (Safe-PackagePath $Path)){throw 'Status is unavailable or linked.'}
    $stream=[IO.FileStream]::new($Path,[IO.FileMode]::Open,[IO.FileAccess]::Read,
        [IO.FileShare]::ReadWrite-bor [IO.FileShare]::Delete)
    try {
        $length=$stream.Length
        if($length-gt 1048576){throw 'Status exceeds the snapshot size limit.'}
        $bytes=[byte[]]::new($length);$at=0
        while($at-lt $length){$read=$stream.Read($bytes,$at,[int]($length-$at));if($read-eq 0){throw 'Status changed during snapshot.'};$at+=$read}
        return ([Text.Encoding]::UTF8.GetString($bytes).TrimStart([char]0xfeff) | ConvertFrom-Json)
    } finally{$stream.Dispose()}
}
function Session-Files([IO.DirectoryInfo]$Directory) {
    $queue=[Collections.Generic.Queue[IO.DirectoryInfo]]::new();$queue.Enqueue($Directory)
    $visited=0
    while($queue.Count-gt 0-and $visited-lt 1024-and $state.scanned-lt $MaximumScanEntries) {
        $at=$queue.Dequeue();$visited++
        if(-not (Safe-PackagePath $at.FullName)){continue}
        $remaining=$MaximumScanEntries-$state.scanned
        try{$children=@(Scan-Entries $at.FullName ($remaining+1))}catch{$notes.Add('Cannot enumerate session folder: '+$at.Name);continue}
        if($children.Count-gt $remaining){$state.scanLimitReached=$true;$children=@($children | Select-Object -First $remaining)}
        foreach($child in $children) {
            $state.scanned++
            if(($child.Attributes-band [IO.FileAttributes]::ReparsePoint)-ne 0){
                Skip-Diagnostic ([ordered]@{ source=(Relative-PackagePath $child.FullName); reason='linked-or-outside-package' });continue
            }
            if($child.PSIsContainer){$queue.Enqueue($child)}
            elseif(Diagnostic-Name $child.Name){$child}
        }
    }
    if($queue.Count-gt 0){$state.scanLimitReached=$true}
}

# KL1 currently stores independent game/host creation identities directly as
# logs/session-UTC-PID-CREATION. Keep their original identities; do not infer
# a parent game solely from a reused PID or require a performance file.
$sessionRoot=Join-Path $package 'logs'
if([IO.Directory]::Exists($sessionRoot)-and (Safe-PackagePath $sessionRoot)) {
    $directories=@(Scan-Entries $sessionRoot ($MaximumScanEntries+1))
    if($directories.Count-gt $MaximumScanEntries){$notes.Add('Game-session directory scan reached its limit; newer unscanned entries may be omitted.');$directories=@($directories | Select-Object -First $MaximumScanEntries)}
    $recent=@($directories |
        Where-Object {$_.PSIsContainer-and $_.Name-match '^session-\d{8}T\d{6}Z-\d+-[0-9a-f]{16}$'-and
            ($_.Attributes-band [IO.FileAttributes]::ReparsePoint)-eq 0} |
        Sort-Object Name -Descending | Select-Object -First $SessionCount)
    foreach($session in $recent) {
        $sessions.Add([ordered]@{ directory=$session.Name; identityScope='independent-process-session' })
        Session-Files $session | ForEach-Object {Copy-Diagnostic $_ (Relative-PackagePath $_.FullName)}
        # A package-owned dump takes priority; never add a second WER dump for
        # that session. No external search is attempted for host/unknown IDs.
        if(-not [IO.File]::Exists((Join-Path $session.FullName 'game.dmp'))){
            $identity=Read-GameSessionIdentity $session
            if($identity){$werCandidates.Add($identity)}
        }
    }
}
foreach($identity in $werCandidates) {
    if(-not $werRoot){break}
    $matching=@($werCandidates | Where-Object {$_.processId-eq $identity.processId-and $_.createUnixSeconds-eq $identity.createUnixSeconds})
    if($matching.Count-ne 1){$notes.Add('WER fallback omitted: selected sessions have ambiguous PID/second creation identity.');continue}
    $path=Join-Path $werRoot ('kaneandlynch.exe.'+$identity.processId+'.dmp')
    if([IO.File]::Exists($path)){
        Copy-Diagnostic (Get-Item -LiteralPath $path -Force) (Join-Path ('logs/'+$identity.session) 'windows-wer.dmp') $identity
    }
}

# Older development packages wrote independent source files directly to logs/.
# Select recent runs per source, preserving all rotations; never infer all
# source identities solely from the newest performance filename.
$legacyRoot=Join-Path $package 'logs'
if([IO.Directory]::Exists($legacyRoot)-and (Safe-PackagePath $legacyRoot)) {
    $legacyEntries=@(Scan-Entries $legacyRoot ($MaximumScanEntries+1))
    if($legacyEntries.Count-gt $MaximumScanEntries){$notes.Add('Legacy file scan reached its entry limit.');$legacyEntries=@($legacyEntries | Select-Object -First $MaximumScanEntries)}
    $legacy=@($legacyEntries | Where-Object {-not $_.PSIsContainer-and $_.Name-match '^(?:performance|process|exceptions|memory-reserve|launcher)-.*\.jsonl(?:\.[1-7])?$'})
    foreach($kind in @('exceptions','process','memory-reserve','performance','launcher')) {
        $groups=@($legacy | Where-Object {$_.Name-match ('^'+$kind+'-')} |
            Group-Object {$_.Name-replace '\.[1-7]$',''} | Sort-Object {($_.Group | Measure-Object LastWriteTimeUtc -Maximum).Maximum} -Descending |
            Select-Object -First $SessionCount)
        foreach($group in $groups){foreach($file in $group.Group){Copy-Diagnostic $file (Join-Path 'legacy' $file.Name)}}
    }
}
$names=@('KaneLynchVR-status.json','KaneLynchVRHost-status.json','KaneLynchOpenXRHost-status.json')
foreach($name in @('vr.ini','launcher.ini','package-ready.json','payload-manifest.json','KaneLynchVR.log',
    'KaneLynchVRHost.log','KaneLynchOpenXRHost.log','KaneLynchVR-launch-error.txt')+$names) {
    $path=Join-Path $package $name
    if([IO.File]::Exists($path)){Copy-Diagnostic (Get-Item -LiteralPath $path -Force) (Join-Path 'package' $name)}
}
if($Seconds-gt 0) {
    $writer=[IO.StreamWriter]::new((Join-Path $output 'samples.jsonl'),$false,$utf8)
    try {
        for($sample=0;$sample-lt $Seconds;$sample++) {
            $record=[ordered]@{ utc=[DateTime]::UtcNow.ToString('o') }
            foreach($name in $names) {
                $path=Join-Path $package $name
                try{$record[$name]=Read-StatusSnapshot $path}
                catch{$record[$name]=@{ unavailable=$true }}
            }
            $line=$record | ConvertTo-Json -Depth 16 -Compress
            if($writer.BaseStream.Length+$utf8.GetByteCount($line)+2-gt $maximumFileBytes){
                $notes.Add('Passive status sampling stopped at the file-size limit.');break
            }
            $writer.WriteLine($line);$writer.Flush()
            if($sample+1-lt $Seconds){Start-Sleep -Seconds 1}
        }
    } finally{$writer.Dispose()}
}
$hashes=@()
foreach($name in @('KaneLynchVR.dll','KaneLynchVRHost.exe','KaneLynchOpenXRHost.exe','KaneLynchVRSettings.exe',
    'KaneLynchVRMonitor.exe','LaunchVR.exe','openxr_loader.dll','renderer-dxvk-x86/d3d9.dll','renderer-d3d9on12-x86/d3d9.dll')) {
    $path=Join-Path $package $name
    if([IO.File]::Exists($path)-and (Safe-PackagePath $path)) {
        try{$hashes+=@{ file=$name; sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() }}
        catch{$notes.Add('Cannot hash package file: '+$name)}
    }
}
[IO.File]::WriteAllText((Join-Path $output 'package-hashes.json'),(ConvertTo-Json -InputObject @($hashes) -Depth 4),$utf8)
$eventEnd=[DateTime]::UtcNow
# Windows PowerShell5 Get-WinEvent treats FilterHashtable date fields as
# local wall-clock values, including a DateTime marked Utc. Pass local query
# bounds; keep explicit record validation and all exported metadata in UTC.
$queryEventStart=$eventStart.ToLocalTime();$queryEventEnd=$eventEnd.ToLocalTime()
# Bound Windows-provider discovery as well as exported records. A crowded
# machine-wide window may omit older events; no protected dump is accessed.
$eventQueryLimit=4096
try {
    # Include a past window: collection is commonly started AFTER the crash.
    $events=@(Get-WinEvent -FilterHashtable @{LogName='Application';StartTime=$queryEventStart;EndTime=$queryEventEnd;Id=1000,1001,1002} -MaxEvents $eventQueryLimit -ErrorAction Stop |
        Where-Object {(Event-InWindow $_ 'Application' $eventEnd)-and $_.Id-in @(1000,1001,1002)-and
            $_.Message-match '(?i)\bKANEANDLYNCH\.EXE\b|\bKaneLynch(?:VR(?:Host|Settings|Monitor)?|OpenXRHost)(?:\.exe)?\b'} |
        Sort-Object TimeCreated -Descending | Select-Object -First 256 | ForEach-Object {Event-Record $_ 'Application'})
    [IO.File]::WriteAllText((Join-Path $output 'crash-events.json'),(ConvertTo-Json -InputObject @($events) -Depth 5),$utf8)
} catch {$notes.Add('Windows crash event log unavailable: '+$_.Exception.Message)}
$gpuEvents=[Collections.Generic.List[object]]::new()
foreach($query in @(@{LogName='System';Id=4101,1001,1019},@{LogName='Application';Id=1001;ProviderName='Windows Error Reporting'})) {
    $query.StartTime=$queryEventStart;$query.EndTime=$queryEventEnd
    try {
        $records=@(Get-WinEvent -FilterHashtable $query -MaxEvents $eventQueryLimit -ErrorAction Stop)
        if($records.Count-ge $eventQueryLimit){$notes.Add('Windows '+$query.LogName+' GPU event query reached its '+$eventQueryLimit+'-record discovery limit; older matching events may be omitted.')}
        foreach($record in $records) {
            if((Event-InWindow $record $query.LogName $eventEnd)-and (GPU-Event $record $query.LogName)){
                $gpuEvents.Add((Event-Record $record $query.LogName))
            }
        }
    } catch {$notes.Add('Windows '+$query.LogName+' GPU event log unavailable: '+$_.Exception.Message)}
}
$gpuReport=[ordered]@{ schema=1; scope='machine-wide-context'; attribution='not-attributed-to-game';
    note='Display/GPU events may belong to other applications. Temporal proximity alone does not attribute a system failure to this game or mod. No kernel dumps are collected.';
    windowStartUtc=$eventStart.ToString('o'); windowEndUtc=$eventEnd.ToString('o'); maximumEvents=256;
    maximumQueriedEventsPerLog=$eventQueryLimit;
    events=@($gpuEvents.ToArray() | Sort-Object {$_['utc']} -Descending | Select-Object -First 256) }
[IO.File]::WriteAllText((Join-Path $output 'gpu-events.json'),($gpuReport | ConvertTo-Json -Depth 6),$utf8)
if($state.scanLimitReached){$notes.Add('Recorded-session content scan reached its directory/entry limit; additional files were not inspected.')}
if($state.enumerationLimitReached){$notes.Add('A directory enumeration reached its entry limit; additional entries were not inspected.')}
$manifest=[ordered]@{ schema=1; game='Kane & Lynch: Dead Men'; sessionLayout='logs/session-UTC-PID-CREATION';
    sessionSelection='latest independent process sessions; game and host are not merged by PID';
    werFallbackSelection='missing game.dmp only; exact selected game manifest PID+creation, canonical LocalAppData/CrashDumps filename, minidump MiscInfo PID+creation seconds';
    collectedUtc=$started.ToString('o'); eventWindowStartUtc=$eventStart.ToString('o'); eventWindowEndUtc=$eventEnd.ToString('o');
    gpuEventSelection='gpu-events.json: bounded machine-wide GPU timeout/recovery context; not automatically attributed to game';
    eventWindowHours=$LookbackHours; requestedSessions=$SessionCount; sessions=@($sessions.ToArray());
    passiveSecondsRequested=$Seconds;
    maximumFileBytes=$maximumFileBytes; maximumTotalBytes=$maximumTotalBytes; maximumFiles=$MaximumFiles;
    maximumScanEntries=$MaximumScanEntries; scannedSessionEntries=$state.scanned; scanLimitReached=($state.scanLimitReached-or $state.enumerationLimitReached);
    skippedDetailLimit=128; omissionCounts=$omissionCounts;
    copiedBytes=$state.bytes; files=@($included.ToArray()); skipped=@($skipped.ToArray()); notes=@($notes.ToArray()) }
[IO.File]::WriteAllText((Join-Path $output 'collection.json'),($manifest | ConvertTo-Json -Depth 8),$utf8)
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory($output,$archive,[IO.Compression.CompressionLevel]::Optimal,$false)
# Only the collector's new GUID-owned staging directory is removed. Source
# sessions and previous exports are never touched, even if a source is active.
$resolvedOutput=[IO.Path]::GetFullPath($output)
if(-not $resolvedOutput.StartsWith($exportRoot.TrimEnd([IO.Path]::DirectorySeparatorChar)+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)-or
   [IO.Path]::GetFileName($resolvedOutput)-ne $exportName){throw 'Collector staging cleanup path escaped its owned export directory.'}
Remove-Item -LiteralPath $resolvedOutput -Recurse -Force
Write-Host "Saved diagnostics: $archive"
Write-Output $archive
