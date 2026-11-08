# Windows installer

Built on GitHub, not on a developer machine:
`.github/workflows/release-windows.yml` compiles Release for both
architectures, builds the ZCode engine from its own repository, stages the
files and runs Inno Setup. Push a tag `v*` and the installer is attached to
the GitHub Release as a normal download; run the workflow by hand from the
Actions tab and it comes out as a workflow artifact instead. (Artifacts need
a GitHub login to download, release assets do not — which is why a real
release goes out as a release asset.)

## One installer, not two

The text encoding is an **input profile**, which is a runtime setting:
`ProfileSwitch` gates the candidate sources, the settings dialog has a
dropdown, and the choice lives in `HKCU\Software\PaizaIME`. Two installers
would ship the same binaries differing only in a default — and could not
coexist in any case, because both would claim the same COM CLSID, the same
TSF language profile (`mn-Mong-CN`) and the same install directory, so the
second would overwrite the first rather than sit beside it.

So ZCode is a **component**. Setup offers *Unicode + ZCode* or *Unicode
Mongolian only*, and the difference on disk is two files plus a font. The
profile can still be switched afterwards; and if someone installs without
ZCode and later selects it, the settings dialog says why the engine is
missing rather than silently producing no candidates.

## What gets installed where, and why

```
<install dir>\
  PaizaService.exe          the service: candidates, settings dialog
  PaizaTsf.dll              64-bit text service
  fixed_sequences.txt       read by the DLL from *its own* directory
  dictionary.txt            read by the service from its own directory
  lexicon.bin               compiled lexicon; the service copes without it
  profiles\unicode.txt
  x86\
    PaizaTsf.dll            32-bit text service
    fixed_sequences.txt     its own copy — ModuleDirectory() is this folder
  zcode_stdio.exe           [ZCode] resolved in the *executable's* directory
  profiles\zcode.txt        [ZCode]
  zcode-cpp\                [ZCode] the engine's GPL-2.0 licence and NOTICE
```

Two of those placements are easy to get wrong and silent when wrong:

* **The 32-bit DLL needs its own `fixed_sequences.txt`.** `PaizaTsf.dll`
  reads it from the directory the *module* is in, and the 32-bit module is
  in `x86\`. Without its copy, 32-bit hosts get no MVS insertion.
* **`zcode_stdio.exe` goes beside `PaizaService.exe`, not in `profiles\`.**
  The host resolves an engine named by a profile inside the executable's
  directory and never searches `PATH` — a profile file that could name a
  path would be arbitrary code execution.

Both architectures are registered with `regsvr32` (64-bit from `{sys}`,
32-bit from `{syswow64}`) and unregistered on uninstall. 32-bit host
applications load the 32-bit text service, and without it the IME is simply
absent in them — which a user has little chance of diagnosing.

## Building it by hand

Needs Visual Studio Build Tools and
[Inno Setup 6](https://jrsoftware.org/isdl.php).

```bat
cmake -S . -B build -A x64
cmake --build build --config Release
cmake --install build --config Release --prefix stage

cmake -S . -B build32 -A Win32
cmake --build build32 --config Release --target PaizaTsf
```

Then assemble the two staged subdirectories the workflow builds — `stage\x86`
(the 32-bit DLL and a copy of `fixed_sequences.txt`) and `stage\zcode`
(`zcode_stdio.exe`, `data\profiles\zcode.txt`, and from the zcode-cpp
checkout its `LICENSE`, `NOTICE.md` and `third_party\fonts\z52haratig.ttf`)
— and compile:

```bat
"C:\Program Files (x86)\Inno Setup 6\ISCC.exe" /DAppVersion=0.1.0-local /DStageDir=..\stage installer\paiza.iss
```

The result lands in `installer\Output\`.

## Languages

English and Simplified Chinese, chosen automatically: `ShowLanguageDialog=no`
with `LanguageDetectionMethod=uilanguage` means a Chinese Windows gets
Chinese and everything else falls back to the first entry, which is English.
No language prompt either way — asking is noise when one of two choices is
the universal fallback.

`i18n/ChineseSimplified.isl` is **vendored on purpose**. Chinese is a
user-contributed translation and is *not* part of the official Inno Setup
distribution: some builds bundle it, a clean Chocolatey install does not,
and the CI runner's did not — which is how this was found, by a red run
after it had compiled perfectly on a developer machine that happened to
have the file. The maintainer, source repository and contact are in the
file's own header, kept intact; it is distributed through
[jrsoftware.org/files/istrans](https://jrsoftware.org/files/istrans/) for
exactly this use. If you need certainty about its licence, that header
names the upstream repository to ask.

To add a language, drop its `.isl` beside that one and add a `[Languages]`
entry plus the `[CustomMessages]` lines; do not reference
`compiler:Languages\`, for the reason above.

## Known gaps

* **Not code-signed.** SmartScreen will warn, and a text service is exactly
  the kind of thing users are warned about, since the DLL is loaded into
  every application. A certificate is the only real fix.
* **Licences in the box.** Paiza is AGPL-3.0 (plus a commercial option) and
  the ZCode engine is GPL-2.0-or-later; they are separate programs
  communicating over a pipe, and both licences ship. The Z52 Hara Tig font
  carries no licence text of its own and is redistributed with the original
  author's permission — see the engine's `NOTICE.md`, which is installed
  with it.
* **Not yet verified on a clean machine.** The script compiles and the
  staged tree is right, but nobody has run the installer on a Windows
  without a development build already present. Registering a text service
  from an elevated process is the part most worth watching.
