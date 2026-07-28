; HybridSlicer - Inno Setup installer script
; Build with: ISCC.exe installer\HybridSlicer.iss
; Output:     dist\HybridSlicer-Setup.exe
; Automatically installs x64 or x86 build based on user system

#define AppName    "HybridSlicer"
#define AppVersion "1.2.0"
#define AppExe     "HybridSlicer.exe"

[Setup]
AppId={{B8F3A2E1-5C4D-4E6F-9A8B-1D2E3F4A5B6C}
AppName={#AppName}
AppVersion={#AppVersion}
SetupIconFile=app-icon.ico
VersionInfoVersion={#AppVersion}
AppPublisherURL=http://localhost:5000
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
OutputDir=..\dist
OutputBaseFilename=HybridSlicer-Setup
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x86 x64 arm64
ArchitecturesInstallIn64BitMode=x64 arm64
UninstallDisplayName={#AppName}
CloseApplications=force
RestartApplications=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional icons:"

[Files]
Source: "..\publish\x64\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion; Check: Is64BitInstallMode
Source: "..\publish\x86\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion solidbreak; Check: not Is64BitInstallMode
Source: "app-icon.ico"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"; IconFilename: "{app}\app-icon.ico"
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; IconFilename: "{app}\app-icon.ico"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExe}"; Description: "Launch {#AppName} now"; Flags: nowait postinstall skipifsilent
Filename: "{app}\{#AppExe}"; Flags: nowait skipifdoesntexist; Check: IsSilentInstall

[Code]
function IsSilentInstall(): Boolean;
begin
  Result := WizardSilent();
end;

function InitializeSetup(): Boolean;
var
  CuraPath: String;
  Found: Boolean;
begin
  Result := True;
  Found := False;
  if RegQueryStringValue(HKLM, 'SOFTWARE\UltiMaker\Cura', 'InstallLocation', CuraPath) then
    Found := FileExists(CuraPath + '\CuraEngine.exe');
  if not Found then
    Found := FileExists('C:\Program Files\UltiMaker Cura 5.12.0\CuraEngine.exe');
  if not Found then
    MsgBox('UltiMaker Cura was not detected. HybridSlicer requires CuraEngine for slicing. Please install UltiMaker Cura (5.10+) before running HybridSlicer. You can continue the installation.', mbInformation, MB_OK);
end;
