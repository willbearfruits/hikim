; HIKIM Windows installer (Inno Setup 6 - preinstalled on GitHub windows runners)
[Setup]
AppName=HIKIM
AppVersion=0.3.0
AppPublisher=characterglitch
DefaultDirName={autopf}\HIKIM
DefaultGroupName=HIKIM
OutputBaseFilename=HIKIM-setup
Compression=lzma2
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64compatible
DisableProgramGroupPage=yes
UninstallDisplayName=HIKIM
LicenseFile=..\LICENSE

[Tasks]
Name: desktopicon; Description: "Desktop shortcut"; Flags: unchecked

[Files]
Source: "..\build\dawglitch_artefacts\Release\HIKIM.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\dawglitch_artefacts\Release\ffmpeg.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\dawglitch_artefacts\Release\ffmpeg-LICENSE.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\HIKIM"; Filename: "{app}\HIKIM.exe"
Name: "{autodesktop}\HIKIM"; Filename: "{app}\HIKIM.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\HIKIM.exe"; Description: "Launch HIKIM"; Flags: nowait postinstall skipifsilent
