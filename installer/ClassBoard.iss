; Windows installer for ClassBoard (Inno Setup 6).
;
; Build the deployed application folder first (see docs/BUILDING.md), then:
;   iscc /DAppVersion=<x.y.z> /DSourceDir=C:\path\to\ClassBoard installer\ClassBoard.iss
; AppVersion must be the project version from CMakeLists.txt (project(... VERSION x.y.z)), so
; the installer, the app and the release tag v<x.y.z> agree. SourceDir is the folder produced
; by "cmake --install" + windeployqt; it must contain bin\ClassBoard.exe. The installer is
; written to installer\Output unless /O is given.
; Optional: /DAnimationDir=<folder> with the frames from make_animation.py adds the board
; animation that plays while ClassBoard installs.

#ifndef AppVersion
  #error Pass the version from CMakeLists.txt: iscc /DAppVersion=x.y.z ...
#endif
#ifndef SourceDir
  #define SourceDir "..\ClassBoard"
#endif

#define AppName "ClassBoard"
#define AppExe "ClassBoard.exe"
#define LessonProgId "ClassBoard.Lesson"

[Setup]
; Never change AppId: it is how Windows recognises updates of an installed ClassBoard.
AppId={{4B703B97-6701-4D9F-9FDC-242D00F76207}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=ClassBoard contributors
AppComments=Digital classroom board for touch displays
VersionInfoVersion={#AppVersion}
VersionInfoDescription={#AppName} Setup
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
; Installs for all users (needs administrator rights); the user can choose "only for me" instead.
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog commandline
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
MinVersion=10.0
LicenseFile=..\LICENSE
SetupIconFile=..\resources\app\classboard.ico
UninstallDisplayIcon={app}\bin\{#AppExe}
UninstallDisplayName={#AppName}
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes
OutputBaseFilename=ClassBoard-Setup-{#AppVersion}-x64
; Updating while ClassBoard is open: close it first (lessons are autosaved).
CloseApplications=yes
RestartApplications=no
ChangesAssociations=yes
SetupLogging=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "fileassoc"; Description: "Open .classboard lessons with ClassBoard"; GroupDescription: "File types:"

[Files]
#ifdef AnimationDir
; The board animation shown while installing (not installed). First, so it unpacks quickly.
Source: "{#AnimationDir}\board*.bmp"; Flags: dontcopy
#endif
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\bin\{#AppExe}"; WorkingDir: "{app}\bin"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\bin\{#AppExe}"; WorkingDir: "{app}\bin"; Tasks: desktopicon
Name: "{autoprograms}\{#AppName} User Guide"; Filename: "{app}\doc\ClassBoard-User-Guide.pdf"

[Registry]
; HKA = all users (HKLM) for an administrator install, the current user (HKCU) otherwise.
Root: HKA; Subkey: "Software\Classes\.classboard"; ValueType: string; ValueName: ""; ValueData: "{#LessonProgId}"; Flags: uninsdeletevalue; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\.classboard\OpenWithProgids"; ValueType: string; ValueName: "{#LessonProgId}"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\{#LessonProgId}"; ValueType: string; ValueName: ""; ValueData: "ClassBoard lesson"; Flags: uninsdeletekey; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\{#LessonProgId}\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\bin\{#AppExe},0"; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\{#LessonProgId}\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\bin\{#AppExe}"" ""%1"""; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}\SupportedTypes"; ValueType: string; ValueName: ".classboard"; ValueData: ""; Flags: uninsdeletekey

[Run]
Filename: "{app}\bin\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

; Saved lessons (in Documents), settings and autosave recovery live in the user's profile and
; are kept when ClassBoard is uninstalled.

[Code]
#ifdef AnimationDir
const
  AnimationFrames = 40;     // make_animation.py: FRAMES
  AnimationWidth = 440;     // make_animation.py: WIDTH, HEIGHT (1x)
  AnimationHeight = 200;
  AnimationInterval = 100;  // ms per frame

var
  AnimationImages: array of TBitmapImage;
  AnimationFrame: Integer;
  AnimationTimer: LongWord;

function SetTimer(hWnd, nIDEvent, uElapse, lpTimerFunc: LongWord): LongWord;
  external 'SetTimer@user32.dll stdcall';
function KillTimer(hWnd, nIDEvent: LongWord): LongWord;
  external 'KillTimer@user32.dll stdcall';

procedure AnimationTick(Wnd, Msg, IdEvent, Time: LongWord);
var
  Next: Integer;
begin
  Next := (AnimationFrame + 1) mod AnimationFrames;
  AnimationImages[Next].Visible := True;
  AnimationImages[AnimationFrame].Visible := False;
  AnimationFrame := Next;
end;

procedure StopAnimation;
begin
  if AnimationTimer <> 0 then
    KillTimer(0, AnimationTimer);
  AnimationTimer := 0;
end;

// The frames go below the progress bar of the Installing page, as large as fits. Screens at
// 175 % scaling or more (such as 4K boards) get the sharper 2x frames.
procedure CreateAnimation;
var
  Page: TNewNotebookPage;
  Prefix, Name: String;
  I, Left, Top, MaxW, MaxH, W, H: Integer;
begin
  Page := WizardForm.InstallingPage;
  if ScaleX(100) >= 175 then
    Prefix := 'board@2x-'
  else
    Prefix := 'board-';
  Left := WizardForm.ProgressGauge.Left;
  Top := WizardForm.ProgressGauge.Top + WizardForm.ProgressGauge.Height + ScaleY(16);
  MaxW := WizardForm.ProgressGauge.Width;
  MaxH := Page.Height - Top;
  W := MaxW;
  H := W * AnimationHeight div AnimationWidth;
  if H > MaxH then
  begin
    H := MaxH;
    W := H * AnimationWidth div AnimationHeight;
  end;
  SetArrayLength(AnimationImages, AnimationFrames);
  for I := 0 to AnimationFrames - 1 do
  begin
    Name := Prefix + Format('%.2d', [I]) + '.bmp';
    ExtractTemporaryFile(Name);
    AnimationImages[I] := TBitmapImage.Create(WizardForm);
    AnimationImages[I].Parent := Page;
    AnimationImages[I].SetBounds(Left + (MaxW - W) div 2, Top, W, H);
    AnimationImages[I].Stretch := True;
    AnimationImages[I].Bitmap.LoadFromFile(ExpandConstant('{tmp}\') + Name);
    AnimationImages[I].Visible := I = 0;
  end;
  AnimationFrame := 0;
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if CurPageID = wpInstalling then
  begin
    if AnimationTimer = 0 then
      AnimationTimer := SetTimer(0, 0, AnimationInterval, CreateCallback(@AnimationTick));
  end
  else
    StopAnimation;
end;

procedure DeinitializeSetup;
begin
  StopAnimation;
end;
#endif

procedure InitializeWizard;
begin
  // A minimize button, so Setup can finish in the background.
  WizardForm.BorderIcons := WizardForm.BorderIcons + [biMinimize];
#ifdef AnimationDir
  CreateAnimation;
#endif
end;
