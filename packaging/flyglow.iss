#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#ifndef StageDir
  #define StageDir "..\dist\FlyGlow"
#endif
#ifndef OutputDir
  #define OutputDir "..\dist"
#endif

#define AppName "FlyGlow"
#define AppExe "FlyGlow.exe"
#define SceneProgId "FlyGlow.Scene"

[Setup]
AppId={{43A939A6-EC8C-4E5C-ACED-8B87142D5FAB}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppName}
VersionInfoVersion={#AppVersion}
VersionInfoProductName={#AppName}
VersionInfoProductVersion={#AppVersion}
VersionInfoDescription={#AppName} Setup
VersionInfoCompany={#AppName}

SetupArchitecture=x64
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763

PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog commandline
UsedUserAreasWarning=no
DefaultDirName={autopf}\{#AppName}
DisableProgramGroupPage=yes
ChangesAssociations=yes
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}

ShowLanguageDialog=yes
WizardStyle=modern dynamic
WizardImageFile=wizard-image-100.png,wizard-image-200.png
WizardSmallImageFile=wizard-small-100.png,wizard-small-200.png
SetupIconFile=..\resources\flyglow.ico

Compression=lzma2/ultra64
SolidCompression=yes
OutputDir={#OutputDir}
OutputBaseFilename=FlyGlow-{#AppVersion}-setup-win64

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "ru"; MessagesFile: "compiler:Languages\Russian.isl"

[CustomMessages]
en.FileAssocGroup=File associations:
ru.FileAssocGroup=Сопоставление файлов:
en.SceneFileType=FlyGlow scene
ru.SceneFileType=Сцена FlyGlow

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "assoc"; Description: "{cm:AssocFileExtension,{#AppName},.flyglow}"; GroupDescription: "{cm:FileAssocGroup}"

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Excludes: "portable.txt,\data"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\FlyGlow\FlyGlow\ui"; ValueType: string; ValueName: "language"; ValueData: "{language}"; Flags: createvalueifdoesntexist

Root: HKA; Subkey: "Software\Classes\.flyglow"; ValueType: string; ValueName: ""; ValueData: "{#SceneProgId}"; Flags: uninsdeletevalue; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\.flyglow\OpenWithProgids"; ValueType: string; ValueName: "{#SceneProgId}"; ValueData: ""; Flags: uninsdeletevalue; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\{#SceneProgId}"; ValueType: string; ValueName: ""; ValueData: "{cm:SceneFileType}"; Flags: uninsdeletekey; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\{#SceneProgId}\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#AppExe},0"; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\{#SceneProgId}\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}"; Flags: uninsdeletekey; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}\SupportedTypes"; ValueType: string; ValueName: ".flyglow"; ValueData: ""; Tasks: assoc

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent
