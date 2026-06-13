; HIKIM Windows installer (Inno Setup 6). Plug-and-play: the app is static-CRT
; linked (no VC++ redist needed) and ships a bundled ffmpeg so it decodes
; anything out of the box. Build: ISCC.exe installer\hikim.iss
#define AppVer "0.4.0"

[Setup]
AppId={{B1A7C3D2-7E4F-4A11-9C2D-6E0F1A2B3C4D}
AppName=HIKIM
AppVersion={#AppVer}
AppPublisher=characterglitch
AppPublisherURL=https://willbearfruits.github.io/hikim/
DefaultDirName={autopf}\HIKIM
DefaultGroupName=HIKIM
; per-user install: no admin / UAC friction - truly plug-and-play
PrivilegesRequired=lowest
OutputBaseFilename=HIKIM-setup
Compression=lzma2/ultra64
SolidCompression=yes
LZMAUseSeparateProcess=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
DisableProgramGroupPage=yes
DisableWelcomePage=no
WizardStyle=modern
UninstallDisplayName=HIKIM
UninstallDisplayIcon={app}\HIKIM.exe
LicenseFile=..\LICENSE
SetupIconFile=..\packaging\hikim.ico
WizardImageFile=..\packaging\wizard-large.bmp
WizardSmallImageFile=..\packaging\wizard-small.bmp
WizardImageStretch=no

[Tasks]
Name: desktopicon; Description: "Put HIKIM on my desktop"; Flags: unchecked

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
Filename: "{app}\HIKIM.exe"; Description: "Unleash HIKIM"; Flags: nowait postinstall skipifsilent

; ---- the fun part: HIKIM-voiced wizard, not the generic boilerplate ----
[Messages]
SetupWindowTitle=HIKIM Setup - mega-beta, handle with care
SetupAppTitle=HIKIM Setup
WelcomeLabel1=Let's wire up HIKIM
WelcomeLabel2=HIKIM is a mega-beta DAW built for creative destruction - TEETH to corrupt, WIRES to patch, a clean path for when you behave.%n%nThis drops HIKIM and a bundled ffmpeg on your machine, so it plays anything you throw at it. No drivers, no runtimes, no nonsense - just one exe and its teeth.%n%nKeep backups of anything you love. This thing bites.
LicenseLabel3=HIKIM is free software under the AGPLv3 (so is JUCE; RubberBand is GPL). Read it, or don't - but it's yours to share, break, and rebuild.
WizardReady=One breath before the noise
ReadyLabel1=HIKIM is about to land here:
ReadyLabel2a=Hit Install and it'll wire itself up. Last chance to back out clean.
FinishedHeadingLabel=HIKIM is loose.
FinishedLabel=Go break something beautiful. Drop a break, feed it TEETH, ride a knob until it screams - then bounce it before it heals. Press F1 in-app if you forget how.
FinishedLabelNoIcons=HIKIM is installed. Go make a glorious mess.
ClickFinish=Click Finish to set HIKIM loose.
ExitSetupTitle=Leaving so soon?
ExitSetupMessage=Bail on the install? HIKIM stays caged for now. Run this again whenever you're ready to misbehave.
ConfirmUninstall=Send HIKIM away? Your sessions and audio stay put - only the app leaves. You can always invite the chaos back.
BeveledLabel=characterglitch - handle with care
