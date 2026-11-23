#ifndef PayloadDir
  #error PayloadDir is required; use build-installer.ps1
#endif
#ifndef ModVersion
  #error ModVersion is required
#endif
#ifndef OutputDir
  #error OutputDir is required
#endif
#ifndef ModReleaseLabel
  #define ModReleaseLabel ModVersion
#endif
#ifndef ModReleaseSlug
  #define ModReleaseSlug ModVersion
#endif

[Setup]
#ifdef InstallerSmoke
AppId={{820E5F33-768D-4D59-9B77-92AC142D8BB4}
AppName=Kane & Lynch: Dead Men VR Installer Test
DefaultDirName={localappdata}\KaneLynchVR-Installer-Test
DefaultGroupName=Kane & Lynch Dead Men VR Installer Test
#else
AppId={{D9D21F05-61B2-4FC7-92AC-C1DE240F9D0A}
AppName=Kane & Lynch: Dead Men VR
DefaultDirName={localappdata}\KaneLynchVR
DefaultGroupName=Kane & Lynch Dead Men VR
#endif
AppVersion={#ModVersion}
AppVerName=Kane & Lynch: Dead Men VR {#ModReleaseLabel}
VersionInfoVersion={#ModVersion}
VersionInfoDescription=Kane & Lynch: Dead Men VR {#ModReleaseLabel} Setup
AppPublisher=Kane & Lynch: Dead Men VR project
AppComments=Kane & Lynch: Dead Men VR {#ModReleaseLabel}
UsePreviousAppDir=yes
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
MinVersion=10.0
ArchitecturesAllowed=x64compatible
OutputDir={#OutputDir}
OutputBaseFilename=KaneLynchVR-{#ModReleaseSlug}-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=Kane & Lynch: Dead Men VR {#ModReleaseLabel}
UninstallDisplayIcon={app}\KaneLynchVRSettings.exe
CloseApplications=no
RestartApplications=no
SetupLogging=no
DisableDirPage=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create launch and settings shortcuts on the desktop"

[Files]
Source: "{#PayloadDir}\*"; DestDir: "{app}"; Excludes: "*.ini"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#PayloadDir}\vr.ini"; DestDir: "{app}"; Flags: onlyifdoesntexist uninsneveruninstall
Source: "{#PayloadDir}\launcher.ini"; DestDir: "{app}"; Flags: onlyifdoesntexist uninsneveruninstall

[Icons]
#ifdef InstallerSmoke
Name: "{userprograms}\Kane & Lynch Dead Men VR Installer Test"; Filename: "{app}\KaneLynchVRSettings.exe"; Parameters: "--launch"; WorkingDir: "{app}"
Name: "{userprograms}\Kane & Lynch Dead Men VR Installer Test Settings"; Filename: "{app}\KaneLynchVRSettings.exe"; WorkingDir: "{app}"
Name: "{userdesktop}\Kane & Lynch Dead Men VR Installer Test"; Filename: "{app}\KaneLynchVRSettings.exe"; Parameters: "--launch"; WorkingDir: "{app}"; Tasks: desktopicon
Name: "{userdesktop}\Kane & Lynch Dead Men VR Installer Test Settings"; Filename: "{app}\KaneLynchVRSettings.exe"; WorkingDir: "{app}"; Tasks: desktopicon
#else
Name: "{userprograms}\Kane & Lynch Dead Men VR"; Filename: "{app}\KaneLynchVRSettings.exe"; Parameters: "--launch"; WorkingDir: "{app}"
Name: "{userprograms}\Kane & Lynch Dead Men VR Settings"; Filename: "{app}\KaneLynchVRSettings.exe"; WorkingDir: "{app}"
Name: "{userdesktop}\Kane & Lynch Dead Men VR"; Filename: "{app}\KaneLynchVRSettings.exe"; Parameters: "--launch"; WorkingDir: "{app}"; Tasks: desktopicon
Name: "{userdesktop}\Kane & Lynch Dead Men VR Settings"; Filename: "{app}\KaneLynchVRSettings.exe"; WorkingDir: "{app}"; Tasks: desktopicon
#endif

[Run]
Filename: "{app}\KaneLynchVRSettings.exe"; Description: "Open Kane & Lynch: Dead Men VR settings"; Flags: postinstall nowait skipifsilent unchecked
Filename: "{app}\README.md"; Description: "Read the installation and controls guide"; Flags: shellexec postinstall skipifsilent unchecked

[Code]
var
  GamePage: TInputFileWizardPage;
  SelectedGame: String;
  MetadataCompatible: Boolean;
  PreviousInstall, PreviousUninstaller, SetupHelper, CleanupSnapshot, DiscoveryFile: String;

function ModAppId: String;
begin
#ifdef InstallerSmoke
  Result := '820E5F33-768D-4D59-9B77-92AC142D8BB4';
#else
  Result := 'D9D21F05-61B2-4FC7-92AC-C1DE240F9D0A';
#endif
end;

function UninstallKey: String;
begin
#ifdef InstallerSmoke
  Result := 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{820E5F33-768D-4D59-9B77-92AC142D8BB4}_is1';
#else
  Result := 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{D9D21F05-61B2-4FC7-92AC-C1DE240F9D0A}_is1';
#endif
end;

function InitializeSetup: Boolean;
begin
#ifdef InstallerSmoke
  Result := CompareText(ExpandConstant('{param:FIXTUREAPPID|}'), '820E5F33-768D-4D59-9B77-92AC142D8BB4') = 0;
#else
  Result := ExpandConstant('{param:FIXTUREAPPID|}') = '';
#endif
end;

function AbsolutePath(const Value: String): Boolean;
begin
  Result := False;
  if (Length(Value) < 3) or (Length(Value) >= 260) or (Pos('"', Value) <> 0) or
     (Pos(#13, Value) <> 0) or (Pos(#10, Value) <> 0) then Exit;
  Result := ((Value[2] = ':') and ((Value[3] = '\') or (Value[3] = '/'))) or (Copy(Value, 1, 2) = '\\');
end;

function CanonicalFolder(const Value: String): String;
begin
  Result := RemoveBackslashUnlessRoot(ExpandFileName(Value));
  StringChangeEx(Result, '/', '\', True);
end;

function CheckIdle(const Helper, Folder: String): String;
var ExitCode: Integer;
begin
  Result := '';
  if not Exec(Helper, '--check-install-path "' + Folder + '"', ExpandConstant('{tmp}'), SW_HIDE, ewWaitUntilTerminated, ExitCode) then
    Result := 'Could not inspect running mod processes. Close Kane & Lynch: Dead Men and its VR launcher, then retry.'
  else if ExitCode = 4 then
    Result := 'Kane & Lynch: Dead Men VR is running from this folder. Save your progress and close the game and launcher, then retry. The installer does not close them.'
  else if ExitCode <> 0 then
    Result := 'Could not inspect running mod processes. Close Kane & Lynch: Dead Men and its VR launcher, then retry.';
end;

function QuotedToken(const Line: String; var At: Integer; var Token: String): Boolean;
var Ch: Char;
begin
  Result := False; Token := '';
  while (At <= Length(Line)) and (Line[At] <= ' ') do At := At + 1;
  if At > Length(Line) then Exit;
  if Line[At] <> '"' then Exit;
  At := At + 1;
  while At <= Length(Line) do begin
    Ch := Line[At]; At := At + 1;
    if Ch = '"' then begin Result := True; Exit; end;
    if (Ch = '\') and (At <= Length(Line)) then
      if (Line[At] = '\') or (Line[At] = '"') then begin Ch := Line[At]; At := At + 1; end;
    Token := Token + Ch;
  end;
end;

function EntryValue(const FileName, Key: String): String;
var Lines: TArrayOfString; I, At: Integer; Name, Value: String;
begin
  Result := '';
  if not LoadStringsFromFile(FileName, Lines) then Exit;
  for I := 0 to GetArrayLength(Lines) - 1 do begin
    At := 1;
    if QuotedToken(Lines[I], At, Name) and QuotedToken(Lines[I], At, Value) then
      if CompareText(Name, Key) = 0 then begin Result := Value; Exit; end;
  end;
end;

function LibraryGame(const LibraryPath: String): String;
var Folder, Candidate: String;
begin
  Result := '';
  Folder := EntryValue(AddBackslash(LibraryPath) + 'steamapps\appmanifest_8080.acf', 'installdir');
  if (Folder = '') or (Pos('\', Folder) <> 0) or (Pos('/', Folder) <> 0) or (Folder = '.') or (Folder = '..') then Folder := 'Kane and Lynch Dead Men';
  Candidate := AddBackslash(LibraryPath) + 'steamapps\common\' + Folder + '\KANEANDLYNCH.EXE';
  if FileExists(Candidate) then Result := Candidate;
end;

function FindSteamGame: String;
var SteamPath, Installed, Name, Value: String; Lines: TArrayOfString; I, At: Integer;
begin
  Result := '';
  if RegQueryStringValue(HKLM32, 'Software\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 8080', 'InstallLocation', Installed) then begin
    Installed := AddBackslash(Installed) + 'KANEANDLYNCH.EXE';
    if FileExists(Installed) then begin Result := Installed; Exit; end;
  end;
  if not RegQueryStringValue(HKCU, 'Software\Valve\Steam', 'SteamPath', SteamPath) then
    if not RegQueryStringValue(HKLM32, 'Software\Valve\Steam', 'InstallPath', SteamPath) then Exit;
  Result := LibraryGame(SteamPath); if Result <> '' then Exit;
  if not LoadStringsFromFile(AddBackslash(SteamPath) + 'steamapps\libraryfolders.vdf', Lines) then Exit;
  for I := 0 to GetArrayLength(Lines) - 1 do begin
    At := 1;
    if QuotedToken(Lines[I], At, Name) and QuotedToken(Lines[I], At, Value) then
      if (CompareText(Name, 'path') = 0) or ((StrToIntDef(Name, -1) >= 0) and AbsolutePath(Value)) then begin
        Result := LibraryGame(Value); if Result <> '' then Exit;
      end;
  end;
end;

function ExistingFolder: String;
begin
  Result := '';
  if not RegQueryStringValue(HKCU32, UninstallKey, 'InstallLocation', Result) then
    RegQueryStringValue(HKCU64, UninstallKey, 'InstallLocation', Result);
end;

function ExistingUninstaller: String;
var Value: String; At: Integer;
begin
  Result := '';
  if not RegQueryStringValue(HKCU32, UninstallKey, 'UninstallString', Value) then
    RegQueryStringValue(HKCU64, UninstallKey, 'UninstallString', Value);
  Value := Trim(Value);
  if Value = '' then Exit;
  if Value[1] = '"' then begin
    At := 2;
    while (At <= Length(Value)) and (Value[At] <> '"') do At := At + 1;
    if At <= Length(Value) then Result := Copy(Value, 2, At - 2);
  end else if AbsolutePath(Value) then Result := Value;
  if not AbsolutePath(Result) then Result := '';
end;

function ManagePrevious(const Phase: String): String;
var PowerShell, Arguments, Report: String; ExitCode: Integer;
begin
  Result := '';
  PowerShell := ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe');
  Report := ExpandConstant('{tmp}\KaneLynchVR-migration-' + Phase + '.json');
  Arguments := '-NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "' + SetupHelper + '"' +
    ' -Phase ' + Phase + ' -NewDirectory "' + CanonicalFolder(WizardDirValue) + '"' +
    ' -GameExe "' + SelectedGame + '" -AppId ' + ModAppId + ' -ResultFile "' + Report + '"' +
    ' -SnapshotFile "' + CleanupSnapshot + '"';
  if PreviousInstall <> '' then Arguments := Arguments + ' -PreviousDirectory "' + PreviousInstall + '"';
  if PreviousUninstaller <> '' then Arguments := Arguments + ' -UninstallerPath "' + PreviousUninstaller + '"';
  if Phase = 'Preflight' then begin
    Arguments := Arguments + ' -DiscoveryFile "' + DiscoveryFile + '"';
#ifdef InstallerSmoke
    Arguments := Arguments + ' -DefaultDirectory "' + ExpandConstant('{localappdata}\KaneLynchVR-Installer-Test') + '"';
#else
    Arguments := Arguments + ' -DefaultDirectory "' + ExpandConstant('{localappdata}\KaneLynchVR') + '"';
#endif
  end;
  if not Exec(PowerShell, Arguments, ExpandConstant('{tmp}'), SW_HIDE, ewWaitUntilTerminated, ExitCode) then
    Result := 'Could not prepare the previous installation safely. Check folder permissions and retry.'
  else if ExitCode <> 0 then
    Result := 'Could not safely use the selected mod folders. Choose a separate new folder or a verified Kane & Lynch: Dead Men VR installation, outside game and Windows folders. Avoid nested folders and links. Existing files have not been replaced.';
  Log('Previous-installation phase ' + Phase + '; report: ' + Report + '; result: ' + Result);
end;

function ExistingGame: String;
var Folder: String;
begin
  Result := ''; Folder := ExistingFolder;
  if not AbsolutePath(Folder) then Exit;
  Result := GetIniString('Launcher', 'GameExe', '', AddBackslash(Folder) + 'launcher.ini');
  if (Result <> '') and not AbsolutePath(Result) then Result := ExpandFileName(AddBackslash(Folder) + Result);
end;

function ValidateGame: Boolean;
var Hash, Helper: String; ExitCode: Integer;
begin
  Result := False; MetadataCompatible := False; SelectedGame := Trim(GamePage.Values[0]);
  if not AbsolutePath(SelectedGame) or not FileExists(SelectedGame) or
     (CompareText(ExtractFileName(SelectedGame), 'KANEANDLYNCH.EXE') <> 0) then begin
    SuppressibleMsgBox('Select an existing KANEANDLYNCH.EXE. Quotes, relative paths and line breaks are not allowed.', mbError, MB_OK, IDOK); Exit;
  end;
  try Hash := Lowercase(GetSHA256OfFile(SelectedGame));
  except SuppressibleMsgBox('Could not read the game executable.', mbError, MB_OK, IDOK); Exit; end;
  try ExtractTemporaryFile('KaneLynchVRSetupGuard.exe');
  except SuppressibleMsgBox('Could not prepare the executable compatibility check.', mbError, MB_OK, IDOK); Exit; end;
  Helper := ExpandConstant('{tmp}\KaneLynchVRSetupGuard.exe');
  if not Exec(Helper, '--verify-game "' + SelectedGame + '"', ExpandConstant('{tmp}'), SW_HIDE, ewWaitUntilTerminated, ExitCode) then begin
    SuppressibleMsgBox('Could not verify the game executable.' + #13#10 + 'SHA256: ' + Hash, mbError, MB_OK, IDOK); Exit;
  end;
  if ExitCode = 3 then begin
    SuppressibleMsgBox('This game executable is not supported. Select a supported original Kane & Lynch: Dead Men executable.' + #13#10 + 'SHA256: ' + Hash, mbError, MB_OK, IDOK); Exit;
  end;
  if (ExitCode <> 0) and (ExitCode <> 10) then begin
    SuppressibleMsgBox('Could not verify the game executable.' + #13#10 + 'SHA256: ' + Hash, mbError, MB_OK, IDOK); Exit;
  end;
  MetadataCompatible := ExitCode = 10;
  if MetadataCompatible then Log('Warning: exact EXE hash is new; the complete normalized game code and layout match, only audited PE metadata differs.');
  Result := True;
end;

procedure InitializeWizard;
var Candidate: String;
begin
  GamePage := CreateInputFilePage(wpWelcome, 'Select Kane & Lynch: Dead Men',
    'Steam libraries are searched automatically. You can select the game executable yourself.',
    'Choose KANEANDLYNCH.EXE from your installed game. The mod will be installed separately on the next page.');
  GamePage.Add('Game executable:', 'Kane & Lynch: Dead Men executable|KANEANDLYNCH.EXE|Executable files|*.exe', '.exe');
  Candidate := ExpandConstant('{param:GAMEEXE|}');
  if Candidate = '' then Candidate := ExistingGame;
  if Candidate = '' then Candidate := GetPreviousData('GameExe', '');
  if Candidate = '' then Candidate := FindSteamGame;
  GamePage.Values[0] := Candidate;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var Helper, ModPath, GamePath: String;
begin
  Result := '';
  if not ValidateGame then begin Result := 'Select a supported KANEANDLYNCH.EXE.'; Exit; end;
  ModPath := CanonicalFolder(WizardDirValue); GamePath := CanonicalFolder(ExtractFileDir(SelectedGame));
  if (CompareText(ModPath, GamePath) = 0) or (Pos(Lowercase(AddBackslash(GamePath)), Lowercase(AddBackslash(ModPath))) = 1) or
     (Pos(Lowercase(AddBackslash(ModPath)), Lowercase(AddBackslash(GamePath))) = 1) then begin
    Result := 'Choose a separate folder for the mod, outside the game folder.'; Exit;
  end;
  if Length(ModPath) >= 220 then begin Result := 'Choose a shorter mod installation path (under 220 characters).'; Exit; end;
  PreviousInstall := ExistingFolder; PreviousUninstaller := ExistingUninstaller;
  if not AbsolutePath(PreviousInstall) then begin PreviousInstall := ''; PreviousUninstaller := ''; end
  else PreviousInstall := CanonicalFolder(PreviousInstall);
  Helper := ExpandConstant('{tmp}\KaneLynchVRSetupGuard.exe');
  Result := CheckIdle(Helper, ModPath);
  if Result <> '' then Exit;
  ExtractTemporaryFile('KaneLynchVRSetupCleanup.ps1'); SetupHelper := ExpandConstant('{tmp}\KaneLynchVRSetupCleanup.ps1');
  CleanupSnapshot := ExpandConstant('{tmp}\KaneLynchVR-previous-ownership.json');
  DiscoveryFile := ExpandConstant('{tmp}\KaneLynchVR-previous-discovery.ini');
  DeleteFile(CleanupSnapshot); DeleteFile(DiscoveryFile);
  Result := ManagePrevious('Preflight');
  if Result <> '' then Exit;
  PreviousInstall := ''; PreviousUninstaller := '';
  if GetIniString('Previous', 'Recognized', '0', DiscoveryFile) = '1' then begin
    PreviousInstall := GetIniString('Previous', 'Directory', '', DiscoveryFile);
    PreviousUninstaller := GetIniString('Previous', 'Uninstaller', '', DiscoveryFile);
    if not AbsolutePath(PreviousInstall) then begin Result := 'The previous installation could not be verified.'; Exit; end;
    if CompareText(PreviousInstall, ModPath) <> 0 then Result := CheckIdle(Helper, PreviousInstall);
  end;
end;

procedure SaveGameSelection;
var Helper, Mode: String; ExitCode: Integer;
begin
  Helper := ExpandConstant('{app}\KaneLynchVRSetupGuard.exe');
  Mode := '--save-game-selection';
  if CompareText(ExpandFileName(SelectedGame), ExpandFileName(FindSteamGame)) = 0 then Mode := '--save-auto-selection';
  if not Exec(Helper, Mode + ' "' + ExpandConstant('{app}') + '" "' + SelectedGame + '"', ExpandConstant('{tmp}'), SW_HIDE, ewWaitUntilTerminated, ExitCode) or (ExitCode <> 0) then
    RaiseException('Could not save the selected game in launcher.ini.');
  if not Exec(Helper, '--apply-release-profile "' + ExpandConstant('{app}') + '"', ExpandConstant('{tmp}'), SW_HIDE, ewWaitUntilTerminated, ExitCode) or (ExitCode <> 0) then
    RaiseException('Could not apply the quiet release profile to vr.ini.');
end;

procedure CurStepChanged(CurStep: TSetupStep);
var Message: String;
begin
  if CurStep = ssInstall then begin
    Message := ManagePrevious('Migrate'); if Message <> '' then RaiseException(Message);
  end;
  if CurStep = ssPostInstall then begin
    SaveGameSelection;
    if PreviousInstall <> '' then begin
      Message := CheckIdle(ExpandConstant('{app}\KaneLynchVRSetupGuard.exe'), PreviousInstall);
      if Message = '' then Message := ManagePrevious('Cleanup');
      if Message <> '' then begin
        Log('The new mod is installed, but some previous files were retained: ' + Message);
        SuppressibleMsgBox('The new mod is installed. Some old files could not be removed safely and were retained. ' + Message, mbInformation, MB_OK, IDOK);
      end;
    end;
  end;
end;

procedure RegisterPreviousData(PreviousDataKey: Integer);
begin
  SetPreviousData(PreviousDataKey, 'GameExe', SelectedGame);
end;

function UpdateReadyMemo(Space, NewLine, MemoUserInfoInfo, MemoDirInfo, MemoTypeInfo, MemoComponentsInfo, MemoGroupInfo, MemoTasksInfo: String): String;
begin
  Result := '';
  if MetadataCompatible then Result := 'Compatibility notice: this exact EXE hash is new. Its complete normalized code and layout match a supported build; only audited PE metadata differs. This edition still needs gameplay testing.' + NewLine + NewLine;
  Result := Result + 'Game:' + NewLine + Space + SelectedGame + NewLine + NewLine + MemoDirInfo + NewLine + NewLine +
    'Choose the matching headset connection and renderer in VR settings. The game is discovered through Steam; a manual game selection is saved locally. Game resolution is preserved.' + NewLine +
    'Existing settings are preserved; VR starts when you launch the mod. Choosing a new folder copies settings when possible and removes verified old mod files after installation succeeds. Edited and unknown files remain in the old folder. Missing old folders do not block installation. Diagnostic logging, testing invulnerability and the optional video recorder are off in public settings; previous recorded sessions are preserved. Handheld aiming uses the laser; mounted guns retain the original crosshair. Use Record-KaneLynch-VR.cmd when recording is wanted.' + NewLine +
    'The game and saved games are not changed by this installer.' + NewLine + NewLine + MemoTasksInfo;
end;

function InitializeUninstall: Boolean;
var Message, PowerShell, Arguments: String; ExitCode: Integer;
begin
  Message := CheckIdle(ExpandConstant('{app}\KaneLynchVRSetupGuard.exe'), ExpandConstant('{app}'));
  if Message = '' then begin
    PowerShell := ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe');
    Arguments := '-NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "' +
      ExpandConstant('{app}\KaneLynchVRSetupCleanup.ps1') + '" -Phase Idle -NewDirectory "' + ExpandConstant('{app}') + '"';
    if not Exec(PowerShell, Arguments, ExpandConstant('{tmp}'), SW_HIDE, ewWaitUntilTerminated, ExitCode) or (ExitCode <> 0) then
      Message := 'A recorder or script may still be running from this installation. Finish recording and close it before uninstalling. No process was stopped.';
  end;
  Result := Message = '';
  if not Result then SuppressibleMsgBox(Message, mbError, MB_OK, IDOK);
end;
