; Inno Setup script for OBS CSS Stinger Forge.
; Built in CI (see .github/workflows/build-project.yaml). Expects:
;   installer\stage\plugin\forge-transitions\...   (the built OBS plugin, designer page included)
#ifndef AppVersion
  #define AppVersion "0.3.4"
#endif

[Setup]
AppId={{B6F2D0B1-5C3E-4E7A-9A44-7E1F3D6A2C10}
AppName=OBS CSS Stinger Forge
AppVersion={#AppVersion}
AppPublisher=Love Thy Nerd
AppPublisherURL=https://lovethynerd.com
; OBS loads plugins from %ProgramData%\obs-studio\plugins\<name>\bin\64bit and \data
DefaultDirName={commonappdata}\obs-studio\plugins\forge-transitions
UsePreviousAppDir=no
DisableDirPage=yes
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

[InstallDelete]
; Leftovers from v0.1.0, which shipped a separate Node.js helper
Type: filesandordirs; Name: "{commonappdata}\LoveThyNerd\OBSCSSStingerForge"
Type: filesandordirs; Name: "{commonappdata}\LoveThyNerd\TransitionForge"

[Dirs]
; The built-in designer saves logos next to the page.
Name: "{app}\data\designer"; Permissions: users-modify

[Files]
Source: "stage\plugin\forge-transitions\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[UninstallDelete]
Type: filesandordirs; Name: "{app}"

[Code]
function InitializeSetup(): Boolean;
begin
  Result := True;
  if MsgBox('Please close OBS Studio before continuing, so it can load the plugin the next time it starts.' + #13#10 + #13#10 + 'Continue with the installation?', mbInformation, MB_YESNO) = IDNO then
    Result := False;
end;
