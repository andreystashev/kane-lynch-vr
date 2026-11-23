param(
    [string]$PackageDirectory=$PSScriptRoot,
    [switch]$ShowConsole,
    [switch]$Stop,
    [uint32]$GamePid=0,
    [switch]$ResolveOnly,
    [switch]$NoAudio,
    [string]$AudioDevice='',
    [string]$OutputDirectory=''
)
$ErrorActionPreference='Stop'
$folder=[IO.Path]::GetFullPath($PackageDirectory)
if (-not (Test-Path -LiteralPath (Join-Path $folder 'KaneLynchVRHost.exe') -PathType Leaf)) {
    throw 'Open the recorder from a built or installed mod package, or specify -PackageDirectory.'
}
$script=Join-Path $folder 'tools-read\record-vr.py'
if(-not (Test-Path -LiteralPath $script)){throw 'This package has no recorder. Use the package containing tools-read\record-vr.py.'}
if($ResolveOnly){Write-Output $folder;exit 0}
$arguments=@($script,'--package',$folder)
if($Stop){
    & python @arguments --quit
    exit $LASTEXITCODE
}
# The controller resolves the running game and matching package on Ctrl+F10.
$arguments+=@('--hotkeys','--game-pid',[string]$GamePid)
if($NoAudio){$arguments+='--no-audio'}
if($AudioDevice){$arguments+=@('--audio-device',$AudioDevice)}
if($OutputDirectory){$arguments+=@('--output-directory',[IO.Path]::GetFullPath($OutputDirectory))}
if($ShowConsole){
    & python @arguments
    exit $LASTEXITCODE
}
$python=Get-Command pythonw.exe -ErrorAction Stop
# Windows native argument quoting, including embedded quotes and trailing
# backslashes. No command shell receives these path/device arguments.
function ConvertTo-NativeArgument([string]$Value){
    if($Value -notmatch '[\s"]' -and $Value){return $Value}
    '"'+[regex]::Replace([regex]::Replace($Value,'(\\*)"','$1$1\"'),'(\\+)$','$1$1')+'"'
}
$arguments+='--background'
$command=($arguments | ForEach-Object {ConvertTo-NativeArgument $_}) -join ' '
Start-Process -FilePath $python.Source -ArgumentList $command -WorkingDirectory $folder -WindowStyle Hidden
