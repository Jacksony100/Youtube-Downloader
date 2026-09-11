#ifndef AppVersion
  #error AppVersion must be supplied by the release script from CMake
#endif

#ifndef PackageDir
  #error PackageDir must be supplied by the release script
#endif

#ifndef OutputDir
  #define OutputDir "..\dist"
#endif

[Setup]
AppId={{8A41D437-8D5E-4C38-B76C-12BF4EFD657A}
AppName=Video Downloader Pro
AppVersion={#AppVersion}
AppVerName=Video Downloader Pro {#AppVersion}
AppPublisher=Jacksony100
AppPublisherURL=https://github.com/Jacksony100/Youtube-Downloader
AppSupportURL=https://github.com/Jacksony100/Youtube-Downloader/issues
AppUpdatesURL=https://github.com/Jacksony100/Youtube-Downloader/releases
DefaultDirName={localappdata}\Programs\VideoDownloaderPro
DefaultGroupName=Video Downloader Pro
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#OutputDir}
OutputBaseFilename=VideoDownloaderPro-Setup-{#AppVersion}
SetupIconFile=..\icon.ico
UninstallDisplayIcon={app}\VideoDownloaderPro.exe
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no
Uninstallable=not IsSmokeTest
CreateUninstallRegKey=not IsSmokeTest
UsePreviousAppDir=not IsSmokeTest
VersionInfoVersion={#AppVersion}.0
VersionInfoCompany=Jacksony100
VersionInfoDescription=Video Downloader Pro Installer
VersionInfoProductName=Video Downloader Pro
VersionInfoProductVersion={#AppVersion}

[Languages]
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#PackageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\Video Downloader Pro"; Filename: "{app}\VideoDownloaderPro.exe"; WorkingDir: "{app}"; Check: not IsSmokeTest
Name: "{autodesktop}\Video Downloader Pro"; Filename: "{app}\VideoDownloaderPro.exe"; WorkingDir: "{app}"; Tasks: desktopicon; Check: not IsSmokeTest

[Run]
Filename: "{app}\VideoDownloaderPro.exe"; Description: "{cm:LaunchProgram,Video Downloader Pro}"; Flags: nowait postinstall skipifsilent

[Code]
function IsSmokeTest: Boolean;
begin
  Result := ExpandConstant('{param:SMOKETEST|0}') = '1';
end;
