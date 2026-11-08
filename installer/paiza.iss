; Paiza Mongolian IME — Windows installer (Inno Setup 6)
;
; One installer, not two. The text encoding is an *input profile*, which is
; a runtime setting: ProfileSwitch gates the candidate sources, the settings
; dialog has a dropdown, and the choice lives in HKCU\Software\PaizaIME.
; Two installers would ship the same binaries differing only in a default —
; and they could not coexist anyway, because both would claim the same COM
; CLSID, the same TSF language profile (mn-Mong-CN) and the same install
; directory. So ZCode is a *component*: two extra files, and the profile can
; still be switched afterwards.
;
; Built by .github/workflows/release.yml; see installer/README.md to run it
; by hand. Expects a staged tree:
;
;   stage\            cmake --install of the x64 Release build
;   stage\x86\        PaizaTsf.dll (Win32) + its own fixed_sequences.txt
;   stage\zcode\      zcode_stdio.exe, zcode.txt, the font, the GPL notices
;
; The split matters: PaizaTsf.dll reads fixed_sequences.txt from *the
; module's* directory, so the 32-bit copy in x86\ needs its own; and an
; engine named by a profile is resolved in the executable's directory, so
; zcode_stdio.exe installs beside PaizaService.exe rather than in profiles\.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef StageDir
  #define StageDir "..\stage"
#endif

[Setup]
AppId={{8F6C2A41-5B3E-4C7A-9D21-6E4F0A8B1C53}
AppName=Paiza Mongolian IME
AppVersion={#AppVersion}
AppPublisher=hugohu789-droid
AppPublisherURL=https://github.com/hugohu789-droid/paiza
DefaultDirName={autopf}\Paiza Mongolian IME
DefaultGroupName=Paiza Mongolian IME
OutputBaseFilename=PaizaMongolianIME-{#AppVersion}-win
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
; A text service is loaded into other applications and registers machine
; wide, so this is an admin install. Never "lowest": a half-registered TIP
; is worse than none.
PrivilegesRequired=admin
; 64-bit install so {sys} is System32 and {syswow64} is SysWOW64 — both are
; needed, one regsvr32 per architecture.
ArchitecturesInstallIn64BitMode=x64compatible
ArchitecturesAllowed=x64compatible
LicenseFile={#StageDir}\LICENSE
UninstallDisplayName=Paiza Mongolian IME

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "zh"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"

[CustomMessages]
en.ZCodeComp=ZCode form-code support (external engine, GPL-2.0-or-later)
en.ZCodeFontComp=Z52 Hara Tig font (ZCode is unreadable without a Z52 font)
en.CoreComp=Input method (required)
zh.ZCodeComp=ZCode 形码支持(外部引擎,GPL-2.0-or-later)
zh.ZCodeFontComp=Z52 Hara Tig 字体(没有 Z52 字体时 ZCode 无法阅读)
zh.CoreComp=输入法主体(必需)

[Types]
Name: "full"; Description: "Unicode + ZCode"
Name: "unicode"; Description: "Unicode Mongolian only"
Name: "custom"; Description: "Custom"; Flags: iscustom

[Components]
Name: "core"; Description: "{cm:CoreComp}"; Types: full unicode custom; \
    Flags: fixed
Name: "zcode"; Description: "{cm:ZCodeComp}"; Types: full
Name: "zcode\font"; Description: "{cm:ZCodeFontComp}"; Types: full

[Files]
; --- core, x64 -----------------------------------------------------------
Source: "{#StageDir}\PaizaService.exe"; DestDir: "{app}"; \
    Components: core; Flags: ignoreversion
Source: "{#StageDir}\PaizaTsf.dll"; DestDir: "{app}"; \
    Components: core; Flags: ignoreversion
Source: "{#StageDir}\dictionary.txt"; DestDir: "{app}"; \
    Components: core; Flags: ignoreversion
Source: "{#StageDir}\lexicon.bin"; DestDir: "{app}"; \
    Components: core; Flags: ignoreversion skipifsourcedoesntexist
; Read by the DLL from its own directory, so it sits beside PaizaTsf.dll.
Source: "{#StageDir}\fixed_sequences.txt"; DestDir: "{app}"; \
    Components: core; Flags: ignoreversion
Source: "{#StageDir}\profiles\unicode.txt"; DestDir: "{app}\profiles"; \
    Components: core; Flags: ignoreversion
Source: "{#StageDir}\LICENSE"; DestDir: "{app}"; \
    Components: core; Flags: ignoreversion
Source: "{#StageDir}\COMMERCIAL-LICENSE.md"; DestDir: "{app}"; \
    Components: core; Flags: ignoreversion

; --- core, 32-bit text service ------------------------------------------
; 32-bit host applications load the 32-bit TIP; without this, the IME is
; simply absent in them, which is hard for a user to diagnose. Its own copy
; of fixed_sequences.txt because ModuleDirectory() is this folder.
Source: "{#StageDir}\x86\PaizaTsf.dll"; DestDir: "{app}\x86"; \
    Components: core; Flags: ignoreversion
Source: "{#StageDir}\x86\fixed_sequences.txt"; DestDir: "{app}\x86"; \
    Components: core; Flags: ignoreversion

; --- ZCode ---------------------------------------------------------------
; Resolved in the executable's directory, so beside PaizaService.exe.
Source: "{#StageDir}\zcode\zcode_stdio.exe"; DestDir: "{app}"; \
    Components: zcode; Flags: ignoreversion
Source: "{#StageDir}\zcode\zcode.txt"; DestDir: "{app}\profiles"; \
    Components: zcode; Flags: ignoreversion
; The engine is GPL-2.0-or-later and a separate work; its licence and
; attribution travel with it.
Source: "{#StageDir}\zcode\LICENSE"; DestDir: "{app}\zcode-cpp"; \
    DestName: "LICENSE"; Components: zcode; Flags: ignoreversion
Source: "{#StageDir}\zcode\NOTICE.md"; DestDir: "{app}\zcode-cpp"; \
    Components: zcode; Flags: ignoreversion

; The font carries no licence text of its own (see zcode-cpp NOTICE.md);
; it is redistributed with the original author's permission.
Source: "{#StageDir}\zcode\z52haratig.ttf"; DestDir: "{autofonts}"; \
    FontInstall: "Z52 Hara Tig"; Components: zcode\font; \
    Flags: onlyifdoesntexist uninsneveruninstall

[Icons]
Name: "{group}\Paiza settings"; Filename: "{app}\PaizaService.exe"; \
    Parameters: "--settings"
Name: "{group}\Uninstall Paiza Mongolian IME"; Filename: "{uninstallexe}"

[Run]
; Register both text services. /s keeps regsvr32 quiet; failure is reported
; by the installer rather than by a message box behind the wizard.
Filename: "{sys}\regsvr32.exe"; Parameters: "/s ""{app}\PaizaTsf.dll"""; \
    StatusMsg: "Registering the text service (64-bit)..."; \
    Components: core
Filename: "{syswow64}\regsvr32.exe"; \
    Parameters: "/s ""{app}\x86\PaizaTsf.dll"""; \
    StatusMsg: "Registering the text service (32-bit)..."; \
    Components: core

[UninstallRun]
; Unregister before the files go, and in the reverse order.
Filename: "{syswow64}\regsvr32.exe"; \
    Parameters: "/s /u ""{app}\x86\PaizaTsf.dll"""; \
    RunOnceId: "UnregTsf32"; Flags: skipifdoesntexist
Filename: "{sys}\regsvr32.exe"; Parameters: "/s /u ""{app}\PaizaTsf.dll"""; \
    RunOnceId: "UnregTsf64"; Flags: skipifdoesntexist

[Code]
// The service is launched on demand by the DLL and keeps running; it has to
// be gone before its files can be replaced or deleted.
procedure StopService();
var
  ResultCode: Integer;
begin
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/f /im PaizaService.exe',
       '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  StopService();
  Result := '';
end;

function InitializeUninstall(): Boolean;
begin
  StopService();
  Result := True;
end;
