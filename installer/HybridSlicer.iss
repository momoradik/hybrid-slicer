; HybridSlicer - Inno Setup installer script
; Build with: ISCC.exe installer\HybridSlicer.iss
; Output:     dist\HybridSlicer-Setup.exe
; Self-contained: .NET runtime is bundled, no external install needed

#define AppName    "HybridSlicer"
#define AppVersion "1.4.1"
#define AppExe     "HybridSlicer.exe"

[Setup]
AppId={{B8F3A2E1-5C4D-4E6F-9A8B-1D2E3F4A5B6C}
AppName={#AppName}
AppVersion={#AppVersion}
SetupIconFile=app-icon.ico
VersionInfoVersion={#AppVersion}
AppPublisherURL=http://localhost:8080
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

[InstallDelete]
; [Files] only adds and overwrites — it never removes files that a previous version
; installed and this one no longer ships. The web bundles are content-hashed, so every
; release leaves its predecessor behind: installs were accumulating orphaned
; index-*.js/css going back months. A browser holding a stale index.html could then
; still resolve the old bundle and keep showing an old UI indefinitely.
; Wipe the web assets before copying so the install always matches the release exactly.
Type: filesandordirs; Name: "{app}\wwwroot\assets"

[Files]
Source: "..\publish\app\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion
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

  // Check for CuraEngine (non-blocking warning)
  Found := False;
  if RegQueryStringValue(HKLM, 'SOFTWARE\UltiMaker\Cura', 'InstallLocation', CuraPath) then
    Found := FileExists(CuraPath + '\CuraEngine.exe');
  if not Found then
    Found := FileExists('C:\Program Files\UltiMaker Cura 5.12.0\CuraEngine.exe');
  if not Found then
    MsgBox('UltiMaker Cura was not detected. HybridSlicer requires CuraEngine for slicing. Please install UltiMaker Cura (5.12+) before running HybridSlicer. You can continue the installation.', mbInformation, MB_OK);
end;
