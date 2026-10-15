; Inno Setup script for M Veditor. Build with:  ISCC /DAppDir="..\dist\M Veditor" /DOutDir="..\dist" MVeditor.iss
#ifndef AppDir
  #define AppDir "..\dist\M Veditor"
#endif
#ifndef OutDir
  #define OutDir "..\dist"
#endif
#define AppName "M Veditor"
#define AppVersion "1.0.0"
#define AppExe "M Veditor.exe"

[Setup]
AppId={{6B1C7D3E-4F2A-4D8B-9E55-3A6F0C1B7A21}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=M Veditor
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
UninstallDisplayIcon={app}\{#AppExe}
OutputDir={#OutDir}
OutputBaseFilename=M Veditor Setup
Compression=lzma2/max
SolidCompression=yes
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
MinVersion=10.0
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
WizardStyle=modern
LicenseFile={#AppDir}\LICENSE
SetupIconFile=..\resources\icon.ico
VersionInfoVersion={#AppVersion}
VersionInfoProductName={#AppName}
ChangesAssociations=yes

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional shortcuts:"
Name: "assoc"; Description: "Open .mveproj project files with M Veditor"; GroupDescription: "File association:"

[Files]
Source: "{#AppDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Registry]
Root: HKA; Subkey: "Software\Classes\.mveproj"; ValueType: string; ValueData: "MVeditor.Project"; Flags: uninsdeletevalue; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\MVeditor.Project"; ValueType: string; ValueData: "M Veditor project"; Flags: uninsdeletekey; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\MVeditor.Project\DefaultIcon"; ValueType: string; ValueData: "{app}\{#AppExe},0"; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\MVeditor.Project\shell\open\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: assoc

[Run]
Filename: "{app}\{#AppExe}"; Description: "Launch {#AppName}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: filesandordirs; Name: "{app}\data"
