param(
    [ValidateSet('Preflight','Migrate','Cleanup','Idle')][string]$Phase='Preflight',
    [string]$PreviousDirectory='',
    [Parameter(Mandatory=$true)][string]$NewDirectory,
    [string]$GameExe='',
    [string]$AppId='',
    [string]$UninstallerPath='',
    [string]$ResultFile='',
    [string]$SnapshotFile='',
    [string]$DiscoveryFile='',
    [string]$DefaultDirectory=''
)
$ErrorActionPreference='Stop'
# This helper never executes an old uninstaller or deletes a directory tree.
# Old ownership is a scoped local marker, never authority for arbitrary paths.
if(-not ('Kl1CleanupPaths' -as [type])){
Add-Type -TypeDefinition @"
using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using Microsoft.Win32.SafeHandles;
public static class Kl1CleanupPaths {
    [StructLayout(LayoutKind.Sequential)] struct FileInfo {
        public uint Attributes; public System.Runtime.InteropServices.ComTypes.FILETIME Created,Accessed,Written;
        public uint Serial,SizeHigh,SizeLow,Links,IndexHigh,IndexLow;
    }
    [StructLayout(LayoutKind.Sequential)] struct Disposition { public byte DeleteFile; }
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern IntPtr CreateFileW(string p,uint access,uint share,IntPtr security,uint creation,uint flags,IntPtr template);
    [DllImport("kernel32.dll",SetLastError=true)] static extern bool GetFileInformationByHandle(IntPtr h,out FileInfo info);
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern uint GetFinalPathNameByHandleW(IntPtr h,System.Text.StringBuilder p,uint count,uint flags);
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern uint GetFileAttributesW(string p);
    [DllImport("kernel32.dll",SetLastError=true)] static extern bool SetFileInformationByHandle(IntPtr h,int kind,ref Disposition data,uint bytes);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
    static readonly IntPtr Invalid=new IntPtr(-1);
    static string Final(IntPtr h) {
        var value=new System.Text.StringBuilder(32768);uint count=GetFinalPathNameByHandleW(h,value,32768,0);
        if(count==0||count>=32768)throw new IOException("Could not resolve a filesystem handle.");
        string p=value.ToString();if(p.StartsWith(@"\\?\UNC\",StringComparison.OrdinalIgnoreCase))return @"\\"+p.Substring(8);
        return p.StartsWith(@"\\?\",StringComparison.Ordinal)?p.Substring(4):p;
    }
    public static bool Under(string path,string root) {return path.StartsWith(root.TrimEnd('\\')+@"\",StringComparison.OrdinalIgnoreCase);}
    public static string Resolve(string requested,bool missing,bool file) {
        if(String.IsNullOrEmpty(requested)||requested.IndexOfAny(new[]{'\r','\n','\0','"','*','?','<','>','|'})>=0||requested.StartsWith(@"\\.\")||requested.StartsWith(@"\\?\"))throw new IOException("Unsafe filesystem path.");
        string value=requested.Replace('/','\\');
        bool drive=value.Length>=3&&Char.IsLetter(value[0])&&value[1]==':'&&value[2]=='\\';
        if(!drive&&!value.StartsWith(@"\\"))throw new IOException("An absolute filesystem path is required.");
        if(value.IndexOf(':',drive?2:0)>=0)throw new IOException("Alternate data streams are refused.");
        string full=Path.GetFullPath(value),root=Path.GetPathRoot(full);
        var parts=full.Substring(root.Length).Split(new[]{'\\'},StringSplitOptions.RemoveEmptyEntries);
        foreach(string part in value.Substring(Path.GetPathRoot(value).Length).Split(new[]{'\\'},StringSplitOptions.RemoveEmptyEntries))if(part=="."||part==".."||part.EndsWith(".")||part.EndsWith(" "))throw new IOException("Ambiguous filesystem path refused.");
        string current=root;
        for(int i=-1;i<parts.Length;i++) {
            if(i>=0)current=Path.Combine(current,parts[i]);
            uint attributes=GetFileAttributesW(current);
            if(attributes==0xffffffff) {int error=Marshal.GetLastWin32Error();if(missing&&(error==2||error==3))return full.TrimEnd('\\');throw new IOException("A path cannot be inspected.");}
            if((attributes&0x400)!=0)throw new IOException("Filesystem links and junctions are refused.");
            if((i<parts.Length-1||!file)&&(attributes&0x10)==0)throw new IOException("A path component is not a directory.");
            IntPtr handle=CreateFileW(current,0x80,7,IntPtr.Zero,3,0x02200000,IntPtr.Zero);
            if(handle==Invalid)throw new IOException("Could not inspect a filesystem component.");
            try {FileInfo info;if(!GetFileInformationByHandle(handle,out info)||(info.Attributes&0x400)!=0)throw new IOException("Filesystem link refused.");if(!String.Equals(Final(handle).TrimEnd('\\'),current.TrimEnd('\\'),StringComparison.OrdinalIgnoreCase))throw new IOException("Filesystem alias refused.");} finally {CloseHandle(handle);}
        }
        return full.TrimEnd('\\');
    }
    public static string DeleteMatching(string path,string root,string expected) {
        Resolve(path,false,true);
        IntPtr handle=CreateFileW(path,0x80010000,1,IntPtr.Zero,3,0x00200000,IntPtr.Zero);
        if(handle==Invalid)return "retained";
        try {
            FileInfo info;if(!GetFileInformationByHandle(handle,out info)||(info.Attributes&0x410)!=0||info.Links!=1||!Under(Final(handle),root))return "retained";
            string digest;using(var stream=new FileStream(new SafeFileHandle(handle,false),FileAccess.Read))using(var hash=SHA256.Create())digest=BitConverter.ToString(hash.ComputeHash(stream)).Replace("-","").ToLowerInvariant();
            if(!String.Equals(digest,expected,StringComparison.OrdinalIgnoreCase))return "modified";
            var disposition=new Disposition{DeleteFile=1};return SetFileInformationByHandle(handle,4,ref disposition,1)?"deleted":"retained";
        }finally {CloseHandle(handle);}
    }
    [DllImport("shlwapi.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern bool PathRelativePathToW(System.Text.StringBuilder output,string from,uint fromAttributes,string to,uint toAttributes);
    public static bool RebaseRuntimeConfig(string config,string previousRoot,string newRoot) {
        Resolve(config,false,true);Resolve(previousRoot,false,false);Resolve(newRoot,false,false);
        byte[] bytes=File.ReadAllBytes(config);if(bytes.Length>4194304)throw new IOException("Configuration is too large.");
        string text;
        if(bytes.Length>=2&&bytes[0]==255&&bytes[1]==254){if((bytes.Length&1)!=0)throw new IOException("Malformed Unicode configuration.");text=System.Text.Encoding.Unicode.GetString(bytes,2,bytes.Length-2);}
        else {
            int start=bytes.Length>=3&&bytes[0]==239&&bytes[1]==187&&bytes[2]==191?3:0;
            try{text=new System.Text.UTF8Encoding(false,true).GetString(bytes,start,bytes.Length-start);}
            catch(System.Text.DecoderFallbackException){text=System.Text.Encoding.Default.GetString(bytes,start,bytes.Length-start);}
        }
        var lines=System.Text.RegularExpressions.Regex.Split(text,"\\r?\\n");string section="",runtime="";int found=-1;
        for(int i=0;i<lines.Length;i++) {
            string line=lines[i].Trim();
            if(line.StartsWith("[")&&line.EndsWith("]")){section=line.Substring(1,line.Length-2).Trim();continue;}
            int at=line.IndexOf('=');if(at<0||!String.Equals(section,"Connection",StringComparison.OrdinalIgnoreCase)||!String.Equals(line.Substring(0,at).Trim(),"RuntimeJson",StringComparison.OrdinalIgnoreCase))continue;
            if(found>=0)throw new IOException("Ambiguous runtime configuration was retained unchanged.");found=i;runtime=line.Substring(at+1).Trim();
        }
        if(found<0||String.IsNullOrEmpty(runtime)||Path.IsPathRooted(runtime))return false;
        string effective;
        try{effective=Path.GetFullPath(Path.Combine(previousRoot,runtime));}catch(ArgumentException){return false;}catch(NotSupportedException){return false;}
        var relative=new System.Text.StringBuilder(32768);string replacement=PathRelativePathToW(relative,newRoot,0x10,effective,0x80)?relative.ToString():effective;
        if(replacement.StartsWith(@".\"))replacement=replacement.Substring(2);
        lines[found]="RuntimeJson="+replacement;
        string updated=String.Join("\r\n",lines),stage=config+".migration-"+Guid.NewGuid().ToString("N")+".tmp";
        Resolve(stage,true,true);
        try {
            using(var output=new FileStream(stage,FileMode.CreateNew,FileAccess.Write,FileShare.None)) {
                byte[] preamble=System.Text.Encoding.Unicode.GetPreamble(),data=System.Text.Encoding.Unicode.GetBytes(updated);
                output.Write(preamble,0,preamble.Length);output.Write(data,0,data.Length);output.Flush(true);
            }
            File.Replace(stage,config,null);return true;
        }finally{if(File.Exists(stage))File.Delete(stage);}
    }
    public static bool CopyConfig(string source,string oldRoot,string destination,string newRoot) {
        Resolve(source,false,true);Resolve(destination,true,true);
        IntPtr handle=CreateFileW(source,0x80000000,1,IntPtr.Zero,3,0x00200000,IntPtr.Zero);
        if(handle==Invalid)return false;
        try {
            FileInfo info;if(!GetFileInformationByHandle(handle,out info)||(info.Attributes&0x410)!=0||info.Links!=1||info.SizeHigh!=0||info.SizeLow>4194304||!Under(Final(handle),oldRoot))return false;
            using(var input=new FileStream(new SafeFileHandle(handle,false),FileAccess.Read))using(var output=new FileStream(destination,FileMode.CreateNew,FileAccess.Write,FileShare.Read)) {
                if(!Under(Final(output.SafeFileHandle.DangerousGetHandle()),newRoot))throw new IOException("Migration destination escaped its folder.");
                input.CopyTo(output);output.Flush(true);
            };return true;
        }finally {CloseHandle(handle);}
    }
}
"@
}
$taskReport=[ordered]@{phase=$Phase;success=$true;stalePrevious=$false;sameDirectory=$false;skippedUnsafePrevious=$false;recognizedPrevious=$false;ownership='none';migrated=@();deleted=@();preserved=@();warnings=@()}
$taskFailure=''
function Same-Path([string]$A,[string]$B){return [string]::Equals($A.TrimEnd('\'),$B.TrimEnd('\'),[StringComparison]::OrdinalIgnoreCase)}
function Protected-Directory([string]$Value,[string]$Game){
    if(Same-Path $Value ([IO.Path]::GetPathRoot($Value))){return $true}
    $taskGameFolder=[IO.Path]::GetDirectoryName($Game)
    if((Same-Path $Value $taskGameFolder) -or [Kl1CleanupPaths]::Under($Value,$taskGameFolder) -or [Kl1CleanupPaths]::Under($taskGameFolder,$Value)){return $true}
    # A previous registration may refer to a different Steam/legacy game copy.
    # Never treat that game directory or any of its children as a mod folder.
    $taskAncestor=$Value
    while($taskAncestor){
        if(Test-Path -LiteralPath (Join-Path $taskAncestor 'KANEANDLYNCH.EXE') -PathType Leaf){return $true}
        $taskParent=[IO.Path]::GetDirectoryName($taskAncestor)
        if(-not $taskParent -or (Same-Path $taskParent $taskAncestor)){break};$taskAncestor=$taskParent
    }
    if($env:WINDIR -and ((Same-Path $Value $env:WINDIR) -or [Kl1CleanupPaths]::Under($Value,$env:WINDIR))){return $true}
    foreach($taskProtected in @($env:USERPROFILE,$env:LOCALAPPDATA,$env:APPDATA,[Environment]::GetFolderPath('Desktop'),[Environment]::GetFolderPath('MyDocuments'),$env:ProgramFiles,${env:ProgramFiles(x86)})){
        if($taskProtected -and (Same-Path $Value $taskProtected)){return $true}
    };return $false
}
$taskAllowed=@('LaunchVR.exe','KaneLynchVR.dll','KaneLynchVRAudio.dll','KaneLynchVRHost.exe','KaneLynchVRMonitor.exe','KaneLynchVRSettings.exe','KaneLynchOpenXRHost.exe','openxr_loader.dll','OPENXR-LICENSE','JSONCPP-LICENSE','OPENVR-LICENSE','THIRD-PARTY-AUDIO.txt','LICENSE','README.md','README-EN.md','README-RU.md','INSTALLATION.md','CONTROLS.md','KNOWN-ISSUES.md','TROUBLESHOOTING.md','THIRD-PARTY-NOTICES.md','RECORDING-RU.md','DIAGNOSTICS-RU.md','KaneLynchVRSetupGuard.exe','KaneLynchVRSetupCleanup.ps1','Launch-KaneLynch-VR.cmd','KaneLynchVRSettings.cmd','Run-VR.cmd','Record-KaneLynch-VR.cmd','Start-KL1-Recorder.ps1','Collect-KL1-Diagnostics.ps1','Collect-KL1-Diagnostics.cmd','Open-Logs.cmd','Check-VR.ps1','analyze_kl1_performance.py','tools-read/record-vr.py','tools-read/decode_visual_trace.py','CHANGES-RU.md','renderer-dxvk-x86/d3d9.dll','renderer-dxvk-x86/LICENSE','renderer-dxvk-x86/provenance.json','renderer-d3d9on12-x86/d3d9.dll','renderer-d3d9on12-x86/NOTICE.txt','renderer-d3d9on12-x86/MINHOOK-LICENSE.txt','renderer-d3d9on12-x86/provenance.json','licenses/MinHook-LICENSE.txt','licenses/Oculus-SDK-LICENSE.txt','licenses/LLVM-LICENSE.txt','licenses/libcxx-LICENSE.txt','licenses/libcxxabi-LICENSE.txt','licenses/libunwind-LICENSE.txt','licenses/compiler-rt-LICENSE.txt','licenses/MinGW-w64-runtime-LICENSE.txt','licenses/winpthreads-LICENSE.txt')
function Check-InterpreterIdle([string]$Folder){
    $taskPrefix=[regex]::Escape($Folder.TrimEnd('\')+'\')
    foreach($taskProcess in @(Get-CimInstance Win32_Process -ErrorAction Stop)){
        if($taskProcess.ProcessId -eq $PID -or $taskProcess.Name -notin @('python.exe','pythonw.exe','powershell.exe','pwsh.exe','cmd.exe')){continue}
        if($taskProcess.CommandLine -and $taskProcess.CommandLine -match ('(?i)(?:^|[\s"])'+$taskPrefix)){
            throw 'A recorder or script is running from this mod folder. Finish recording and close it before updating; no process was stopped.'
        }
    }
}
function Check-DestinationOwnership([string]$Folder,[string]$Id){
    if(-not(Test-Path -LiteralPath $Folder -PathType Container)){return}
    $taskDestinationOwned=$null
    try{$taskDestinationOwned=Read-Ownership $Folder $Id ''}catch{}
    if($taskDestinationOwned -and $taskDestinationOwned.recognized){
        $taskLicense=Join-Path $Folder 'LICENSE'
        if(Test-Path -LiteralPath $taskLicense){
            [void][Kl1CleanupPaths]::Resolve($taskLicense,$false,$true)
            $taskLicenseRecord=$taskDestinationOwned.records | Where-Object file -EQ 'LICENSE' | Select-Object -First 1
            if(-not $taskLicenseRecord -or (Get-FileHash -LiteralPath $taskLicense -Algorithm SHA256).Hash -ine $taskLicenseRecord.sha256){
                throw 'The destination contains an unverified LICENSE file. Choose a separate folder to preserve it.'
            }
        }
        return
    }
    $taskIncomingNames=@($taskAllowed|Where-Object {$_ -ne 'CHANGES-RU.md'})+@('payload-manifest.json','install-identity.json')
    foreach($taskName in $taskIncomingNames){
        $taskCollision=Join-Path $Folder $taskName
        [void][Kl1CleanupPaths]::Resolve($taskCollision,$true,$true)
        if(Test-Path -LiteralPath $taskCollision){
            throw 'This destination contains files from another or unverified mod. Choose a separate folder or a verified Kane & Lynch: Dead Men VR installation. No existing destination files were changed.'
        }
    }
}
function Read-Ownership([string]$Old,[string]$Id,[string]$Uninstaller){
    $taskOwned=[ordered]@{recognized=$false;kind='none';records=@();core=@();uninstaller='';dat='';manifest='';manifestHash=''}
    $taskManifest=Join-Path $Old 'payload-manifest.json'
    $taskPortable=$false
    if(-not(Test-Path -LiteralPath $taskManifest -PathType Leaf)){$taskManifest=Join-Path $Old 'BUILD-INFO.json';$taskPortable=$true}
    if(Test-Path -LiteralPath $taskManifest -PathType Leaf){
        [void][Kl1CleanupPaths]::Resolve($taskManifest,$false,$true)
        if((Get-Item -LiteralPath $taskManifest).Length -gt 4194304){throw 'Old ownership manifest is too large.'}
        $taskData=Get-Content -LiteralPath $taskManifest -Raw -Encoding UTF8|ConvertFrom-Json
        if($taskPortable){
            if($taskData.repository -cne 'kane-lynch-vr' -or [string]$taskData.sourceHead -notmatch '^[a-fA-F0-9]{40}$'){throw 'Old portable ownership marker is not a Kane & Lynch: Dead Men VR package.'}
            $taskRecords=@();foreach($taskProperty in $taskData.files.PSObject.Properties){$taskRecords+=@{file=$taskProperty.Name;sha256=$taskProperty.Value}}
            $taskData=@{schemaVersion=1;product='Kane & Lynch: Dead Men VR';version=$taskData.version;sourceCommit=$taskData.sourceHead;files=$taskRecords}
        }
        if($taskData.product -cne 'Kane & Lynch: Dead Men VR' -or [int]$taskData.schemaVersion -ne 1 -or [string]$taskData.version -notmatch '^\d{1,5}(?:\.\d{1,5}){2,3}$' -or $null -eq $taskData.files -or @($taskData.files).Count -gt 256){throw 'Old ownership manifest is not a Kane & Lynch: Dead Men VR payload.'}
        $taskSeen=@{};$taskCoreMatches=0
        foreach($taskRecord in $taskData.files){
            $taskName=[string]$taskRecord.file;$taskName=$taskName.Replace('\','/')
            if(-not $taskName -or $taskName -match '[:\x00-\x1f<>"|?*]' -or $taskName.StartsWith('/') -or @($taskName.Split('/')|Where-Object {$_ -in @('','.','..') -or $_.EndsWith('.') -or $_.EndsWith(' ')}).Count -or $taskSeen.ContainsKey($taskName)){throw 'Unsafe or duplicate manifest entry refused.'}
            $taskSeen[$taskName]=$true
            if($taskName -in @('vr.ini','mod.ini','launcher.ini')){continue}
            if($taskName -notin $taskAllowed){continue}
            if([string]$taskRecord.sha256 -notmatch '^[a-fA-F0-9]{64}$'){throw 'Old ownership digest is invalid.'}
            $taskPath=Join-Path $Old $taskName
            [void][Kl1CleanupPaths]::Resolve($taskPath,$true,$true)
            $taskOwned.records+=,[ordered]@{file=$taskName;sha256=([string]$taskRecord.sha256).ToLowerInvariant()}
            if($taskName -in @('LaunchVR.exe','KaneLynchVR.dll','KaneLynchVRHost.exe','KaneLynchOpenXRHost.exe') -and (Test-Path -LiteralPath $taskPath -PathType Leaf) -and (Get-Item -LiteralPath $taskPath).Length -gt 0 -and (Get-FileHash -LiteralPath $taskPath -Algorithm SHA256).Hash -ieq $taskRecord.sha256){
                $taskOwned.core+=$taskName;if($taskName -in @('LaunchVR.exe','KaneLynchVR.dll')){$taskCoreMatches++}
            }
        }
        if($taskCoreMatches){$taskOwned.recognized=$true;$taskOwned.kind='manifest';$taskOwned.manifest=$taskManifest;$taskOwned.manifestHash=(Get-FileHash -LiteralPath $taskManifest -Algorithm SHA256).Hash}
    }
    if($Uninstaller){
        try{
            $taskUnins=[Kl1CleanupPaths]::Resolve($Uninstaller,$false,$true)
            if(-not(Same-Path ([IO.Path]::GetDirectoryName($taskUnins)) $Old) -or [IO.Path]::GetFileName($taskUnins) -notmatch '^unins\d{3}\.exe$'){throw 'Uninstaller is outside its installation.'}
            $taskDat=[IO.Path]::ChangeExtension($taskUnins,'.dat');[void][Kl1CleanupPaths]::Resolve($taskDat,$false,$true)
            if((Get-Item -LiteralPath $taskDat).Length -gt 4194304){throw 'Uninstall record is too large.'}
            $taskBytes=[IO.File]::ReadAllBytes($taskDat)
            if($taskBytes.Length -lt 192 -or [Text.Encoding]::ASCII.GetString($taskBytes,0,64).TrimEnd([char]0) -cne 'Inno Setup Uninstall Log (b)' -or [Text.Encoding]::ASCII.GetString($taskBytes,64,128).TrimEnd([char]0) -ine $Id){throw 'Uninstall ownership marker does not match.'}
            $taskText=[Text.Encoding]::Unicode.GetString($taskBytes)
            if($taskText.IndexOf($Old+'\',[StringComparison]::OrdinalIgnoreCase) -lt 0){throw 'Uninstall ownership record belongs to another folder.'}
            $taskOwned.recognized=$true;if($taskOwned.kind -eq 'none'){$taskOwned.kind='uninstall-record'}
            $taskOwned.uninstaller=$taskUnins;$taskOwned.dat=$taskDat
        }catch{$script:taskReport.warnings+='Old uninstaller pair was not verified and was retained.'}
    };return $taskOwned
}
try{
    if($Phase -eq 'Idle'){
        $taskIdle=[Kl1CleanupPaths]::Resolve($NewDirectory,$false,$false);Check-InterpreterIdle $taskIdle
    }else{
    $taskId='{'+([guid]$AppId).ToString().ToUpperInvariant()+'}'
    $taskGame=[Kl1CleanupPaths]::Resolve($GameExe,$false,$true)
    if([IO.Path]::GetFileName($taskGame) -ine 'KANEANDLYNCH.EXE'){throw 'Select the Kane & Lynch: Dead Men game executable.'}
    $taskNew=[Kl1CleanupPaths]::Resolve($NewDirectory,$true,$false)
    if(Protected-Directory $taskNew $taskGame){throw 'Choose a separate dedicated mod folder, outside protected folders and the game.'}
    if($Phase -eq 'Preflight'){Check-DestinationOwnership $taskNew $taskId}
    Check-InterpreterIdle $taskNew
    $taskOld='';$taskOwned=$null
    if($DiscoveryFile){
        if($Phase -ne 'Preflight'){throw 'Automatic discovery is only allowed during preflight.'}
        [void][Kl1CleanupPaths]::Resolve($DiscoveryFile,$true,$true)
        # Inspect a bounded set of meaningful locations. A path or filename
        # alone never authorizes migration, cleanup, or a running-process check.
        $taskCandidates=@($PreviousDirectory,$taskNew,$DefaultDirectory)
        $taskSeenCandidates=@{}
        foreach($taskCandidate in $taskCandidates){
            if(-not $taskCandidate){continue}
            try{
                if($taskCandidate.IndexOfAny([char[]]@([char]0,[char]10,[char]13,[char]34)) -ge 0){throw 'Invalid previous path.'}
                $taskCandidate=[Kl1CleanupPaths]::Resolve($taskCandidate,$true,$false)
                if($taskSeenCandidates.ContainsKey($taskCandidate)){continue};$taskSeenCandidates[$taskCandidate]=$true
                if(Protected-Directory $taskCandidate $taskGame){throw 'Previous folder is protected.'}
                if(-not(Test-Path -LiteralPath $taskCandidate -PathType Container)){continue}
                $taskCandidateUninstaller=if($PreviousDirectory -and (Same-Path $taskCandidate $PreviousDirectory)){$UninstallerPath}else{''}
                $taskCandidateOwned=Read-Ownership $taskCandidate $taskId $taskCandidateUninstaller
                if(-not $taskCandidateOwned.recognized){continue}
                $taskOld=$taskCandidate;$taskOwned=$taskCandidateOwned;break
            }catch{$taskReport.warnings+='An unavailable or unverified automatic previous-install candidate was ignored.'}
        }
        if($taskOld){
            $taskReport.sameDirectory=Same-Path $taskOld $taskNew
            if(-not $taskReport.sameDirectory -and ([Kl1CleanupPaths]::Under($taskOld,$taskNew) -or [Kl1CleanupPaths]::Under($taskNew,$taskOld))){throw 'Previous and new mod folders must be separate, without nesting.'}
            $taskReport.recognizedPrevious=$true;$taskReport.ownership=$taskOwned.kind
        }
    }elseif($PreviousDirectory){
        try{
            $taskOld=[Kl1CleanupPaths]::Resolve($PreviousDirectory,$true,$false)
            if(Protected-Directory $taskOld $taskGame){throw 'Previous folder is protected.'}
        }catch{$taskOld='';$taskReport.skippedUnsafePrevious=$true;$taskReport.warnings+='Unsafe previous installation location was ignored; no old files were changed.'}
    }
    if($taskOld -and -not $DiscoveryFile){
        if(Same-Path $taskOld $taskNew){$taskReport.sameDirectory=$true}
        if(-not $taskReport.sameDirectory -and ([Kl1CleanupPaths]::Under($taskOld,$taskNew) -or [Kl1CleanupPaths]::Under($taskNew,$taskOld))){throw 'Previous and new mod folders must be separate, without nesting.'}
        elseif(-not(Test-Path -LiteralPath $taskOld -PathType Container)){$taskReport.stalePrevious=$true}
        else{
            try{$taskOwned=Read-Ownership $taskOld $taskId $UninstallerPath}catch{$taskReport.skippedUnsafePrevious=$true;$taskReport.warnings+='Invalid previous ownership data was ignored; no old files were changed.'}
            if($taskOwned -and $taskOwned.recognized){$taskReport.recognizedPrevious=$true;$taskReport.ownership=$taskOwned.kind}else{$taskReport.warnings+='Unrecognized previous folder retained. No settings or files were taken from it.'}
        }
    }
    if($taskReport.recognizedPrevious -and -not $taskReport.sameDirectory){Check-InterpreterIdle $taskOld}
    if($SnapshotFile -and $Phase -eq 'Preflight' -and $taskReport.recognizedPrevious){
        [void][Kl1CleanupPaths]::Resolve($SnapshotFile,$true,$true)
        $taskSnapshot=@{product='Kane & Lynch: Dead Men VR cleanup snapshot';appId=$taskId;previous=$taskOld;destination=$taskNew;owned=$taskOwned}
        [IO.File]::WriteAllText($SnapshotFile,($taskSnapshot|ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
    }
    if($SnapshotFile -and $Phase -eq 'Cleanup' -and $taskOld -and (Test-Path -LiteralPath $SnapshotFile -PathType Leaf)){
        [void][Kl1CleanupPaths]::Resolve($SnapshotFile,$false,$true)
        if((Get-Item -LiteralPath $SnapshotFile).Length -gt 4194304){throw 'Cleanup snapshot is too large.'}
        $taskSnapshot=[IO.File]::ReadAllText($SnapshotFile)|ConvertFrom-Json
        if($taskSnapshot.product -cne 'Kane & Lynch: Dead Men VR cleanup snapshot' -or $taskSnapshot.appId -cne $taskId -or -not(Same-Path $taskSnapshot.previous $taskOld) -or -not(Same-Path $taskSnapshot.destination $taskNew)){throw 'Cleanup snapshot does not match this installation.'}
        foreach($taskRecord in $taskSnapshot.owned.records){
            if($taskRecord.file -notin $taskAllowed -or [string]$taskRecord.sha256 -notmatch '^[a-fA-F0-9]{64}$'){throw 'Invalid cleanup snapshot record.'}
            [void][Kl1CleanupPaths]::Resolve((Join-Path $taskOld $taskRecord.file),$true,$true)
        }
        $taskOwned=$taskSnapshot.owned;$taskReport.recognizedPrevious=[bool]$taskOwned.recognized;$taskReport.ownership='preflight-snapshot'
    }
    if($taskReport.recognizedPrevious -and -not $taskReport.sameDirectory -and $Phase -eq 'Migrate'){
        if(-not(Test-Path -LiteralPath $taskNew -PathType Container)){New-Item -ItemType Directory -Path $taskNew -Force|Out-Null}
        [void][Kl1CleanupPaths]::Resolve($taskNew,$false,$false)
        foreach($taskName in @('vr.ini','mod.ini','launcher.ini')){
            $taskSource=Join-Path $taskOld $taskName;$taskDestination=Join-Path $taskNew $taskName
            if(-not(Test-Path -LiteralPath $taskSource -PathType Leaf)){continue}
            if(Test-Path -LiteralPath $taskDestination){$taskReport.preserved+=('destination/'+$taskName);continue}
            try{if([Kl1CleanupPaths]::CopyConfig($taskSource,$taskOld,$taskDestination,$taskNew)){if($taskName -eq 'vr.ini'){[void][Kl1CleanupPaths]::RebaseRuntimeConfig($taskDestination,$taskOld,$taskNew)};$taskReport.migrated+=$taskName}else{$taskReport.warnings+=('Could not safely copy '+$taskName+'; original retained.')}}catch{$taskReport.warnings+=('Could not safely copy '+$taskName+'; original retained.')}
        }
    }
    if($taskReport.recognizedPrevious -and $Phase -eq 'Cleanup'){
        # Never remove an old runnable copy before a complete replacement has
        # actually been copied. This also protects accidental direct invocation.
        try{$taskReplacement=Read-Ownership $taskNew $taskId ''}catch{throw 'The replacement payload could not be verified. Previous files were retained.'}
        foreach($taskRequired in @('LaunchVR.exe','KaneLynchVR.dll','KaneLynchVRHost.exe')){
            if($taskRequired -notin $taskReplacement.core){throw 'The replacement payload is incomplete or modified. Previous files were retained.'}
        }
        $taskNewNames=@($taskReplacement.records|ForEach-Object {$_.file})
        foreach($taskRecord in $taskOwned.records){
            if($taskReport.sameDirectory -and $taskRecord.file -in $taskNewNames){continue}
            $taskPath=Join-Path $taskOld $taskRecord.file
            if(-not(Test-Path -LiteralPath $taskPath -PathType Leaf)){continue}
            try{$taskOutcome=[Kl1CleanupPaths]::DeleteMatching($taskPath,$taskOld,$taskRecord.sha256)}catch{$taskOutcome='retained'}
            if($taskOutcome -eq 'deleted'){$taskReport.deleted+=$taskRecord.file}else{$taskReport.preserved+=$taskRecord.file}
        }
        # A verified uninstall log proves only its named pair, not wildcard
        # unins*.exe or another application's uninstall registration.
        foreach($taskPath in @($taskOwned.uninstaller,$taskOwned.dat)){
            if($taskReport.sameDirectory){continue}
            if(-not $taskPath){continue}
            if($taskPath -eq $taskOwned.dat -and (Test-Path -LiteralPath $taskOwned.uninstaller)){
                $taskReport.preserved+=[IO.Path]::GetFileName($taskPath);continue
            }
            try{$taskDigest=(Get-FileHash -LiteralPath $taskPath -Algorithm SHA256).Hash;$taskOutcome=[Kl1CleanupPaths]::DeleteMatching($taskPath,$taskOld,$taskDigest)}catch{$taskOutcome='retained'}
            if($taskOutcome -eq 'deleted'){$taskReport.deleted+=[IO.Path]::GetFileName($taskPath)}else{$taskReport.preserved+=[IO.Path]::GetFileName($taskPath)}
        }
        if($taskOwned.manifest -and -not $taskReport.sameDirectory){
            try{$taskOutcome=[Kl1CleanupPaths]::DeleteMatching($taskOwned.manifest,$taskOld,$taskOwned.manifestHash)}catch{$taskOutcome='retained'}
            if($taskOutcome -eq 'deleted'){$taskReport.deleted+='payload-manifest.json'}else{$taskReport.preserved+='payload-manifest.json'}
        }
        $taskReport.warnings+='Original settings, recordings, changed files and unknown files remain in the previous folder.'
    }
    }
}catch{$taskReport.success=$false;$taskFailure=$_.Exception.Message;$taskReport.warnings+=$taskFailure}
if($DiscoveryFile -and -not $taskFailure){
    $taskDiscoveredPath=if($taskReport.recognizedPrevious){$taskOld}else{''}
    $taskDiscoveredUninstaller=if($taskReport.recognizedPrevious){$taskOwned.uninstaller}else{''}
    $taskDiscoveryText="[Previous]`r`nRecognized=$([int][bool]$taskReport.recognizedPrevious)`r`nDirectory=$taskDiscoveredPath`r`nUninstaller=$taskDiscoveredUninstaller`r`n"
    [IO.File]::WriteAllText($DiscoveryFile,$taskDiscoveryText,[Text.Encoding]::Unicode)
}
$taskJson=$taskReport|ConvertTo-Json -Depth 8
if($ResultFile){[IO.File]::WriteAllText([IO.Path]::GetFullPath($ResultFile),$taskJson,(New-Object Text.UTF8Encoding($false)))}
Write-Output $taskJson
if($taskFailure){throw $taskFailure}
