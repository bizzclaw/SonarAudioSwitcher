; SonarAudioSwitcher — Inno Setup Installer Script
; Requires Inno Setup 6+ (https://jrsoftware.org/isinfo.php)
;
; Build steps:
;   1. cmake --build build --config Release
;   2. Open this file in Inno Setup Compiler, or run:
;      iscc.exe installer\installer.iss

#define AppName "SonarAudioSwitcher"
#define AppVersion "1.0.1"
#define AppPublisher "SonarAudioSwitcher"
#define AppExeName "SonarAudioSwitcher.exe"

[Setup]
AppId={{B7E3F1A2-8C4D-4F6E-9A1B-2D3E4F5A6B7C}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
OutputDir=..\installer
OutputBaseFilename=SonarAudioSwitcher_Setup
SetupIconFile=..\resources\app.ico
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=lowest
UninstallDisplayIcon={app}\{#AppExeName}
WizardStyle=modern

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
; Main application executable (Release build)
Source: "..\Release\{#AppExeName}"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
; Start Menu shortcut
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExeName}"
; Desktop shortcut (optional, user can uncheck)
Name: "{userdesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"
Name: "startupentry"; Description: "Start with Windows"; GroupDescription: "Startup:"

[Registry]
; "Start with Windows" — only if the user checked the task
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; \
    ValueType: string; ValueName: "{#AppName}"; ValueData: """{app}\{#AppExeName}"""; \
    Flags: uninsdeletevalue; Tasks: startupentry

[Run]
; Offer to launch the app after install
Filename: "{app}\{#AppExeName}"; Description: "Launch {#AppName}"; \
    Flags: nowait postinstall skipifsilent

[UninstallDelete]
; Clean up the %APPDATA% config/log folder on uninstall
Type: files; Name: "{userappdata}\{#AppName}\config.json"
Type: files; Name: "{userappdata}\{#AppName}\sonar_audio_switcher.log"
Type: dirifempty; Name: "{userappdata}\{#AppName}"

