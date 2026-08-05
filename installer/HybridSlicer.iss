; HybridSlicer - Inno Setup installer script
; Build with: ISCC.exe installer\HybridSlicer.iss
; Output:     dist\HybridSlicer-Setup.exe
; Framework-dependent: .NET 8 Desktop Runtime auto-installed if missing

#define AppName    "HybridSlicer"
#define AppVersion "1.2.3"
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

function IsDotNet8Installed(): Boolean;
var
  ResultCode: Integer;
  Output: AnsiString;
  TmpFile: String;
begin
  Result := False;
  TmpFile := ExpandConstant('{tmp}\dotnet_check.txt');
  // Run dotnet --list-runtimes and capture output
  if Exec('cmd.exe', '/C dotnet --list-runtimes > "' + TmpFile + '" 2>&1', '', SW_HIDE, ewWaitUntilTerminated, ResultCode) then
  begin
    if (ResultCode = 0) and LoadStringFromFile(TmpFile, Output) then
    begin
      // Check for any 8.x runtime (desktop or aspnetcore)
      if (Pos('Microsoft.NETCore.App 8.', String(Output)) > 0) or
         (Pos('Microsoft.AspNetCore.App 8.', String(Output)) > 0) then
        Result := True;
    end;
  end;
  DeleteFile(TmpFile);
end;

procedure InstallDotNet8();
var
  ResultCode: Integer;
  DotNetUrl: String;
  InstallerPath: String;
begin
  // Download .NET 8 Desktop Runtime (windowsdesktop runtime includes ASP.NET Core)
  if IsWin64 then
    DotNetUrl := 'https://builds.dotnet.microsoft.com/dotnet/WindowsDesktop/8.0/latest/windowsdesktop-runtime-win-x64.exe'
  else
    DotNetUrl := 'https://builds.dotnet.microsoft.com/dotnet/WindowsDesktop/8.0/latest/windowsdesktop-runtime-win-x86.exe';

  InstallerPath := ExpandConstant('{tmp}\dotnet8-runtime.exe');

  // Download silently using PowerShell
  WizardForm.StatusLabel.Caption := 'Downloading .NET 8 Runtime...';
  WizardForm.StatusLabel.Update;

  if not Exec('powershell.exe',
    '-NoProfile -ExecutionPolicy Bypass -Command "try { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; (New-Object Net.WebClient).DownloadFile(''' + DotNetUrl + ''', ''' + InstallerPath + ''') } catch { exit 1 }"',
    '', SW_HIDE, ewWaitUntilTerminated, ResultCode) or (ResultCode <> 0) then
  begin
    // Fallback: try with curl
    Exec('curl.exe', '-L -o "' + InstallerPath + '" "' + DotNetUrl + '"',
      '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  end;

  // Install silently
  if FileExists(InstallerPath) then
  begin
    WizardForm.StatusLabel.Caption := 'Installing .NET 8 Runtime...';
    WizardForm.StatusLabel.Update;
    Exec(InstallerPath, '/install /quiet /norestart', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssInstall then
  begin
    if not IsDotNet8Installed() then
    begin
      InstallDotNet8();
    end;
  end;
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
