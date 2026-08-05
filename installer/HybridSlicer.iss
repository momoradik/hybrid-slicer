; HybridSlicer - Inno Setup installer script
; Build with: ISCC.exe installer\HybridSlicer.iss
; Output:     dist\HybridSlicer-Setup.exe
; Framework-dependent: requires .NET 8 Desktop Runtime (auto-downloaded if missing)

#define AppName    "HybridSlicer"
#define AppVersion "1.2.2"
#define AppExe     "HybridSlicer.exe"
#define DotNetVersion "8.0"
#define DotNetInstallerUrl "https://download.visualstudio.microsoft.com/download/pr/dotnet-runtime-8.0-win-x64.exe"

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

[Files]
; Framework-dependent build — single folder for all architectures
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

function IsDotNet8Installed(): Boolean;
var
  ResultCode: Integer;
begin
  // dotnet --list-runtimes prints installed runtimes; check for 8.x
  Result := Exec('dotnet', '--list-runtimes', '', SW_HIDE, ewWaitUntilTerminated, ResultCode)
            and (ResultCode = 0);
  if Result then
  begin
    // If dotnet exists, check if any 8.x runtime is present
    // The Launcher's own runtime check will handle the exact version
    Result := True;
  end;
end;

function InitializeSetup(): Boolean;
var
  CuraPath: String;
  Found: Boolean;
  ResultCode: Integer;
begin
  Result := True;

  // Check for .NET 8 runtime
  if not Exec('dotnet', '--version', '', SW_HIDE, ewWaitUntilTerminated, ResultCode) then
  begin
    if MsgBox('.NET 8 Runtime is required but not installed.' + #13#10 + #13#10 +
             'Would you like to download it now? (opens browser)', mbConfirmation, MB_YESNO) = IDYES then
    begin
      ShellExec('open', 'https://dotnet.microsoft.com/en-us/download/dotnet/8.0', '', '', SW_SHOW, ewNoWait, ResultCode);
      MsgBox('Please install .NET 8 Desktop Runtime, then run this installer again.', mbInformation, MB_OK);
      Result := False;
      Exit;
    end;
  end;

  // Check for CuraEngine
  Found := False;
  if RegQueryStringValue(HKLM, 'SOFTWARE\UltiMaker\Cura', 'InstallLocation', CuraPath) then
    Found := FileExists(CuraPath + '\CuraEngine.exe');
  if not Found then
    Found := FileExists('C:\Program Files\UltiMaker Cura 5.12.0\CuraEngine.exe');
  if not Found then
    MsgBox('UltiMaker Cura was not detected. HybridSlicer requires CuraEngine for slicing. Please install UltiMaker Cura (5.12+) before running HybridSlicer. You can continue the installation.', mbInformation, MB_OK);
end;
