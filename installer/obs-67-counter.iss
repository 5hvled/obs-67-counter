#define MyAppName "OBS 67 Counter"
#define MyAppVersion "2.0.0"
#define MyAppPublisher "5hvled"

[Setup]
SourceDir=..
AppId={{8C2510DF-5D8C-46D3-8E26-E0D342A7A667}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={code:GetDefaultOBSDir}
DisableProgramGroupPage=yes
DisableReadyMemo=no
DirExistsWarning=no
OutputDir=release
OutputBaseFilename=OBS-67-Counter-Setup-v{#MyAppVersion}
Compression=lzma2/max
SolidCompression=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
WizardStyle=modern
CloseApplications=yes
RestartApplications=no
Uninstallable=yes
UninstallFilesDir={app}\data\obs-plugins\obs-67-counter\uninstall
SetupLogging=yes
VersionInfoVersion={#MyAppVersion}
VersionInfoProductName={#MyAppName}
VersionInfoProductVersion={#MyAppVersion}
VersionInfoCompany={#MyAppPublisher}

[Files]
Source: "dist\obs-plugins\64bit\obs-67-counter.dll"; DestDir: "{app}\obs-plugins\64bit"; Flags: ignoreversion
Source: "dist\data\obs-plugins\obs-67-counter\onnxruntime.dll"; DestDir: "{app}\data\obs-plugins\obs-67-counter"; Flags: ignoreversion
Source: "dist\data\obs-plugins\obs-67-counter\onnxruntime_providers_shared.dll"; DestDir: "{app}\data\obs-plugins\obs-67-counter"; Flags: ignoreversion skipifsourcedoesntexist
Source: "dist\data\obs-plugins\obs-67-counter\movenet_singlepose_lightning.onnx"; DestDir: "{app}\data\obs-plugins\obs-67-counter"; Flags: ignoreversion
Source: "installer\README-FIRST.txt"; DestDir: "{app}\data\obs-plugins\obs-67-counter"; Flags: ignoreversion
Source: "installer\THIRD-PARTY-NOTICES.txt"; DestDir: "{app}\data\obs-plugins\obs-67-counter"; Flags: ignoreversion

[Run]
Filename: "{app}\bin\64bit\obs64.exe"; Description: "Launch OBS Studio"; Flags: nowait postinstall skipifsilent

[Code]
function GetDefaultOBSDir(Param: String): String;
var
  StandardPath: String;
begin
  StandardPath := ExpandConstant('{autopf}\obs-studio');
  Result := StandardPath;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  ObsExe: String;
begin
  Result := True;

  if CurPageID = wpSelectDir then
  begin
    ObsExe := AddBackslash(WizardDirValue) + 'bin\64bit\obs64.exe';

    if not FileExists(ObsExe) then
    begin
      MsgBox(
        'OBS Studio was not found in that folder.' + #13#10 + #13#10 +
        'Choose the main OBS Studio folder — the folder that contains "bin", "data", and "obs-plugins".' + #13#10 + #13#10 +
        'For a normal installation this is usually:' + #13#10 +
        'C:\Program Files\obs-studio',
        mbError, MB_OK);
      Result := False;
    end;
  end;
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if CurPageID = wpWelcome then
    WizardForm.WelcomeLabel2.Caption :=
      'This installer adds OBS 67 Counter to OBS Studio.' + #13#10 + #13#10 +
      'Before continuing, close OBS Studio if it is running.' + #13#10 + #13#10 +
      'The installer includes the plugin, pose model, and ONNX Runtime — no extra downloads are required.';
end;
