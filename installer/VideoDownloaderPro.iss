#ifndef AppVersion
  #define AppVersion "4.0.2"
#endif

#ifndef PackageDir
  #define PackageDir "..\dist\VideoDownloaderPro-win-x64"
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
Name: "{group}\Video Downloader Pro"; Filename: "{app}\VideoDownloaderPro.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\Video Downloader Pro"; Filename: "{app}\VideoDownloaderPro.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\VideoDownloaderPro.exe"; Description: "{cm:LaunchProgram,Video Downloader Pro}"; Flags: nowait postinstall skipifsilent
