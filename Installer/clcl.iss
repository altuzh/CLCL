; CLCL Inno Setup Script
; Per-user AppData installation (PrivilegesRequired=lowest)

#define MyAppName "CLCL"
#define MyAppVersion "2.2.0"
#define MyAppPublisher "Nakka / CLCL Contributors"
#define MyAppURL "https://github.com/altuzh/CLCL"
#define MyAppExeName "CLCL.exe"

[Setup]
; Unique application GUID for per-user installation
AppId={{8B849767-4DF8-4C82-9CD5-408EB49F16E2}}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
; Non-administrative user AppData installation: {autopf} resolves to {localappdata}\Programs
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
AllowNoIcons=yes
LicenseFile=..\LICENSE
OutputDir=out
OutputBaseFilename=CLCL-Setup-{#MyAppVersion}
SetupIconFile=..\res\icon1.ico
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
CloseApplications=force
CloseApplicationsFilter=CLCL.exe,CLCLSet.exe

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "japanese"; MessagesFile: "compiler:Languages\Japanese.isl"
Name: "german"; MessagesFile: "compiler:Languages\German.isl"
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "ukrainian"; MessagesFile: "compiler:Languages\Ukrainian.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "startup"; Description: "Start CLCL with Windows"; GroupDescription: "Startup:"

[Files]
Source: "..\Release\CLCL.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\Release\CLCLHook.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\Release\CLCLSet.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\readme_en.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\readme_jp.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\readme_de.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\readme_uk.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\readme_zh.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\CLCL Options"; Filename: "{app}\CLCLSet.exe"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon
Name: "{userstartup}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: startup

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent
