; Dante's Inferno installer (Inno Setup 6). Built by package-release.ps1.

#define MyAppName "Dante's Inferno"
#define MyAppPublisher "florinp93"
#define MyAppURL "https://github.com/florinp93/hells-gate-recomp"
#define MyAppExeName "DantesInfernoLauncher.exe"
#define MyAppVersion "0.8.3-beta-hotfix2"

[Setup]
AppId={{DANTES-INFERNO-PC-PORT}}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}/issues
AppUpdatesURL={#MyAppURL}/releases
DefaultDirName={commonpf64}\{#MyAppName}
DefaultGroupName={#MyAppName}
AllowNoIcons=yes
OutputDir=..\beta-release
OutputBaseFilename=DantesInfernoInstaller
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
DisableProgramGroupPage=yes
UsePreviousAppDir=yes
; Updates (launcher "Download & Install") reuse the installed folder.
DisableDirPage=auto
UninstallDisplayIcon={app}\{#MyAppExeName}
UninstallDisplayName={#MyAppName}
SetupIconFile=icon.ico
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "italian"; MessagesFile: "compiler:Languages\Italian.isl"
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "french"; MessagesFile: "compiler:Languages\French.isl"
Name: "brazilianportuguese"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; Port binaries; package-release.ps1 passes GameBuildDir.
#ifndef GameBuildDir
  #define GameBuildDir SourcePath + "\..\out\build\win-amd64-release"
#endif
Source: "{#GameBuildDir}\dantes_inferno.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#GameBuildDir}\rexruntime.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#GameBuildDir}\rexgpu-xenos.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#GameBuildDir}\amd_fidelityfx_dx12.dll"; DestDir: "{app}"; Flags: ignoreversion
#define FfxVkPath GameBuildDir + "\thirdparty\rexglue-sdk\out\win-amd64\amd_fidelityfx_vk.dll"
#if FileExists(FfxVkPath)
Source: "{#FfxVkPath}"; DestDir: "{app}"; Flags: ignoreversion
#endif

; Launcher
Source: "DantesInfernoLauncher\bin\Release\DantesInfernoLauncher.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "DantesInfernoLauncher\bin\Release\DantesInferno.Shared.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "DantesInfernoLauncher\bin\Release\version.txt"; DestDir: "{app}"; Flags: ignoreversion

; Assets
Source: "icon.ico"; DestDir: "{app}"; Flags: ignoreversion
Source: "banner.jpg"; DestDir: "{app}"; Flags: ignoreversion

; Extracts the user's ISO into game\ after install (ExtractIso); no game data is shipped.
Source: "..\tools\extract-xiso\extract-xiso.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall

; Title Update 2, needed for playable DLC such as Trials of Saint Lucia; the
; game applies default.xexp found next to default.xex.
#define TuPath GameBuildDir + "\default.xexp"
#if FileExists(TuPath)
Source: "{#TuPath}"; DestDir: "{app}\game"; Flags: ignoreversion
#endif

[Dirs]
; The launcher (not elevated) saves dantes_inferno.toml here and the game
; writes its logs here, so users need write access under Program Files.
Name: "{app}"; Permissions: users-modify

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\icon.ico"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{commondesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\icon.ico"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Launch Dante's Inferno Launcher"; Flags: postinstall nowait skipifsilent

[InstallDelete]
; Left by earlier versions: the SDK Vulkan build and the D3D12 renderer's
; shader caches, unused by the native renderer.
Type: files; Name: "{app}\dantes_inferno_native.exe"
Type: filesandordirs; Name: "{app}\shader_cache"

[UninstallDelete]
Type: filesandordirs; Name: "{app}\game"
Type: filesandordirs; Name: "{app}\logs"
Type: files; Name: "{app}\dantes_inferno.toml"
Type: files; Name: "{app}\version.txt"
Type: files; Name: "{app}\extract-xiso.log"

[Code]
var
  IsoPage: TInputFileWizardPage;
  IsoSizeMB: Int64;

{ File sizes above 4 GB (ISOs) }
function GetFileSizeEx(hFile: THandle; var lpFileSize: Int64): Boolean;
  external 'GetFileSizeEx@kernel32.dll stdcall';
function CreateFileW(lpFileName: WideString; dwDesiredAccess: Cardinal; dwShareMode: Cardinal; lpSecurityAttributes: Cardinal; dwCreationDisposition: Cardinal; dwFlagsAndAttributes: Cardinal; hTemplateFile: Cardinal): THandle;
  external 'CreateFileW@kernel32.dll stdcall';
function CloseHandle(hObject: THandle): Boolean;
  external 'CloseHandle@kernel32.dll stdcall';

const
  GENERIC_READ = $80000000;
  FILE_SHARE_READ = 1;
  OPEN_EXISTING = 3;
  INVALID_HANDLE_VALUE = $FFFFFFFF;

function GetFileSizeMB(FileName: string): Int64;
var
  hFile: THandle;
  FileSize: Int64;
begin
  Result := 0;
  hFile := CreateFileW(FileName, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
  if hFile <> INVALID_HANDLE_VALUE then
  begin
    if GetFileSizeEx(hFile, FileSize) then
      Result := FileSize div (1024 * 1024);
    CloseHandle(hFile);
  end;
end;

function FormatMB(MB: Int64): string;
begin
  if MB >= 1024 then
    Result := FloatToStr(Round(MB / 1024 * 10) / 10) + ' GB'
  else
    Result := IntToStr(MB) + ' MB';
end;

procedure InitializeWizard;
begin
  { Create a custom page for ISO selection, shown after directory selection }
  IsoPage := CreateInputFilePage(wpSelectDir,
    'Select Game ISO',
    'Choose your Dante''s Inferno Xbox 360 ISO file.',
    'The installer will extract game data from this ISO into the game folder. ' +
    'If you already have game data extracted, you can skip this step.');
  IsoPage.Add('ISO file:', 'ISO files (*.iso)|*.iso|All files (*.*)|*.*', '.iso');
  IsoPage.Values[0] := '';
  IsoSizeMB := 0;
end;

function GameDataPresent: Boolean;
begin
  Result := FileExists(AddBackslash(WizardDirValue) + 'game\default.xex');
end;

{ Updates and reinstalls keep the extracted game: no ISO needed. }
function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := (PageID = IsoPage.ID) and GameDataPresent;
end;

function NeedsExtraction: Boolean;
begin
  Result := (IsoPage.Values[0] <> '');
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  IsoFile: string;
begin
  Result := True;
  { When leaving the ISO selection page, validate and calculate size }
  if CurPageID = IsoPage.ID then
  begin
    IsoFile := IsoPage.Values[0];
    if IsoFile <> '' then
    begin
      if not FileExists(IsoFile) then
      begin
        MsgBox('The selected file does not exist:' + #13#10 + IsoFile, mbError, MB_OK);
        Result := False;
        Exit;
      end;
      IsoSizeMB := GetFileSizeMB(IsoFile);
    end
    else
      IsoSizeMB := 0;
  end;
end;

procedure CurPageChanged(CurPageID: Integer);
var
  BaseMB: Int64;
  TotalMB: Int64;
begin
  if CurPageID = wpReady then
  begin
    { Port binaries and launcher }
    BaseMB := 55;
    if IsoSizeMB > 0 then
    begin
      { Extracted game data plus headroom }
      TotalMB := BaseMB + Round(IsoSizeMB * 1.5);
      WizardForm.DiskSpaceLabel.Caption :=
        'Required space (port + game data from ISO): ' + FormatMB(TotalMB) +
        ' (ISO: ' + FormatMB(IsoSizeMB) + ')';
    end
    else
    begin
      TotalMB := BaseMB;
      WizardForm.DiskSpaceLabel.Caption :=
        'Required space (port only, no ISO): ' + FormatMB(TotalMB) +
        ' — game data will not be extracted.';
    end;
  end;
end;

function UpdateReadyMemo(Space, NewLine, MemoUserInfoInfo, MemoDirInfo, MemoTypeInfo, MemoComponentsInfo, MemoGroupInfo, MemoTasksInfo: string): string;
begin
  Result := MemoDirInfo + NewLine + NewLine +
            'ISO file:' + NewLine + Space;
  if IsoPage.Values[0] <> '' then
  begin
    Result := Result + ExtractFileName(IsoPage.Values[0]);
    if IsoSizeMB > 0 then
      Result := Result + ' (' + FormatMB(IsoSizeMB) + ')';
  end
  else
    Result := Result + '(skipped - game data already extracted)';
end;

{ Not a [Run] entry: Setup ignores their exit codes. Success means
  game\default.xex exists; extract-xiso's output goes to extract-xiso.log. }
procedure ExtractIso;
var
  LogPath: string;
  Params: string;
  ResultCode: Integer;
begin
  LogPath := ExpandConstant('{app}\extract-xiso.log');
  Params := '/C ""' + ExpandConstant('{tmp}\extract-xiso.exe') + '" -x -d "' +
    ExpandConstant('{app}\game') + '" -s "' + IsoPage.Values[0] + '" > "' + LogPath + '" 2>&1"';

  WizardForm.StatusLabel.Caption := 'Extracting game data from ISO (this may take a few minutes)...';
  WizardForm.FilenameLabel.Caption := ExtractFileName(IsoPage.Values[0]);
  WizardForm.ProgressGauge.Style := npbstMarquee;
  try
    if not Exec(ExpandConstant('{cmd}'), Params, ExpandConstant('{app}'), SW_HIDE,
        ewWaitUntilTerminated, ResultCode) then
      ResultCode := -1;
  finally
    WizardForm.ProgressGauge.Style := npbstNormal;
  end;
  Log('extract-xiso exit code: ' + IntToStr(ResultCode));

  if not GameDataPresent then
    SuppressibleMsgBox('The game files could not be extracted from the ISO (exit code ' +
      IntToStr(ResultCode) + ').' + #13#10#13#10 +
      'ISO: ' + IsoPage.Values[0] + #13#10 +
      'Details: ' + LogPath + #13#10#13#10 +
      'Check that the file is a complete Xbox 360 disc image of Dante''s Inferno, ' +
      'then run the installer again. Please attach extract-xiso.log when reporting this.',
      mbError, MB_OK, IDOK);
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  ConfigPath: string;
  VersionFile: string;
begin
  if CurStep = ssPostInstall then
  begin
    if NeedsExtraction then
      ExtractIso;

    { First install: default settings }
    ConfigPath := ExpandConstant('{app}\dantes_inferno.toml');
    if not FileExists(ConfigPath) then
    begin
      SaveStringToFile(ConfigPath,
        'game_data_root = "' + ExpandConstant('{app}\game') + '"' + #13#10 +
        'anisotropic_override = -1' + #13#10 +
        'fullscreen = true' + #13#10 +
        'input_backend = "sdl"' + #13#10 +
        'dlc_source_path = "' + ExpandConstant('{app}\dlc') + '"' + #13#10,
        False);
    end;

    { The launcher's update check reads version.txt }
    VersionFile := ExpandConstant('{app}\version.txt');
    SaveStringToFile(VersionFile, '{#MyAppVersion}' + #13#10, False);
  end;
end;
