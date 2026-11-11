; Inno Setup script for OBS CSS Stinger Forge.
; Built in CI (see .github/workflows/build-project.yaml). Expects:
;   installer\stage\plugin\forge-transitions\...   (the built OBS plugin)
;   installer\stage\node\node.exe                  (portable Node.js)
#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif

[Setup]
AppId={{B6F2D0B1-5C3E-4E7A-9A44-7E1F3D6A2C10}
AppName=OBS CSS Stinger Forge
AppVersion={#AppVersion}
AppPublisher=Love Thy Nerd
AppPublisherURL=https://lovethynerd.com
DefaultDirName={commonappdata}\LoveThyNerd\OBSCSSStingerForge
DisableDirPage=yes
DefaultGroupName=OBS CSS Stinger Forge
DisableProgramGroupPage=yes
OutputDir=..\release
OutputBaseFilename=OBS-CSS-Stinger-Forge-Setup-{#AppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayName=OBS CSS Stinger Forge

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; Flags: unchecked

[Dirs]
; The Forge helper saves logos and presets next to its files, so normal users need write access.
Name: "{app}"; Permissions: users-modify

[Files]
; OBS plugin: OBS loads plugins from %ProgramData%\obs-studio\plugins\<name>\bin\64bit and \data
Source: "stage\plugin\forge-transitions\*"; DestDir: "{commonappdata}\obs-studio\plugins\forge-transitions"; Flags: recursesubdirs createallsubdirs ignoreversion
; Forge designer + helper
Source: "..\forge\*"; DestDir: "{app}"; Excludes: "node_modules\*"; Flags: recursesubdirs createallsubdirs ignoreversion
; Portable Node.js so nothing else needs installing
Source: "stage\node\*"; DestDir: "{app}\node"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{autoprograms}\OBS CSS Stinger Forge\OBS CSS Stinger Forge"; Filename: "{app}\start-forge.bat"; WorkingDir: "{app}"; Comment: "Design CSS stinger transitions for OBS"
Name: "{autoprograms}\OBS CSS Stinger Forge\Install video export (optional)"; Filename: "{app}\install-video-export.bat"; WorkingDir: "{app}"
Name: "{autoprograms}\OBS CSS Stinger Forge\Uninstall OBS CSS Stinger Forge"; Filename: "{uninstallexe}"
Name: "{autodesktop}\OBS CSS Stinger Forge"; Filename: "{app}\start-forge.bat"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\start-forge.bat"; Description: "Open OBS CSS Stinger Forge now"; WorkingDir: "{app}"; Flags: postinstall nowait skipifsilent

[UninstallDelete]
Type: filesandordirs; Name: "{app}"

[Code]
function InitializeSetup(): Boolean;
begin
  Result := True;
  if MsgBox('Please close OBS Studio before continuing, so it can load the new plugin the next time it starts.' + #13#10 + #13#10 + 'Continue with the installation?', mbInformation, MB_YESNO) = IDNO then
    Result := False;
end;
