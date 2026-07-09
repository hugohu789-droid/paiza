# macOS port guide (InputMethodKit)

Status: skeleton written and reviewed on Windows; **compiles in CI's
macos-latest job but has never run on a device**. This document is the
working plan for bringing it up on a Mac.

## Architecture

```
 host app (TextEdit, Safari, ...)
      │  keystrokes / marked text / caret rect   (InputMethodKit XPC)
      ▼
 PaizaMongolian.app          ~/Library/Input Methods/
 ├─ main.mm                  IMKServer bootstrap
 ├─ PaizaInputController     NSEvent -> InputSession calls, mirrors
 │   (paiza_input_controller.mm)   preedit -> setMarkedText:, commit ->
 │                                 insertText:, candidates -> panel
 ├─ PaizaCandidatePanel      non-activating NSPanel
 │   (candidate_panel.mm)    v1: horizontal rows; v2: CoreText vertical
 └─ paiza_core (static lib)  InputSession + CandidateEngine +
                             LocalDictionarySource + KeyMappingStore +
                             localization — SHARED with Windows/Linux
```

Key differences from Windows:

* **No service process.** macOS runs the input method as its own process
  already, so `CandidateEngine` is linked in-process. The named-pipe IPC
  layer is Windows-only by design.
* **All key policy lives in `paiza::InputSession`** (src/core). The
  controller must stay a dumb translator — if you find yourself writing
  input logic in ObjC++, move it into InputSession and add a test in
  tests/input_session_test.cc so all platforms inherit it.

## Feature-parity contract

| Behaviour | Where defined | macOS status |
|---|---|---|
| a–z / Shift second set (`N`→ᠩ …) | InputSession + KeyMapping | wired |
| keymap.txt hot reload | KeyMappingStore (`~/Library/Application Support/PaizaIME/keymap.txt`) | wired (same file format) |
| digits 1–9 commit, Space/Enter/Esc/Backspace | InputSession | wired |
| paging (PgUp/PgDn), selection (arrows) | InputSession | wired |
| dictionary + fallback candidate | CandidateEngine (bundle `Resources/dictionary.txt`) | wired |
| vertical candidate rendering, stem-centred | fontstems.txt calibration | **TODO v2** (panel draws horizontal rows for now) |
| settings dialog (page size, font, language) | core Settings semantics | **TODO** (plan: NSUserDefaults-backed panel; UiStrings from core/localization) |
| en/zh/mn-Cyrillic UI strings | core/localization | ready to consume |

## Bring-up steps on the Mac

1. Prerequisites: full Xcode (not just CLT), `brew install cmake ninja`.
2. Build:
   ```sh
   git clone https://github.com/hugohu789-droid/paiza.git && cd paiza
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug   # add -G Xcode to debug in Xcode
   cmake --build build
   ctest --test-dir build --output-on-failure      # portable tests must pass
   ```
3. Install the input method:
   ```sh
   rm -rf ~/Library/Input\ Methods/PaizaMongolian.app
   cp -R build/bin/PaizaMongolian.app ~/Library/Input\ Methods/
   ```
   Log out/in (first time), then System Settings → Keyboard → Input
   Sources → Edit → “+” → search *Paiza Mongolian* (listed under
   Mongolian). If it does not appear, the ComponentInputModeDict in
   Info.plist is the usual suspect — compare against a working IME like
   [squirrel](https://github.com/rime/squirrel)'s plist.
4. Iterate: after each rebuild,
   ```sh
   pkill PaizaMongolian; cp -R build/bin/PaizaMongolian.app ~/Library/Input\ Methods/
   ```
   The system relaunches the process on the next keystroke. Logs:
   Console.app, filter “PaizaIME” / “PaizaMongolian”, or `log stream
   --predicate 'process == "PaizaMongolian"'`. To debug, attach Xcode/lldb
   to the running PaizaMongolian process (it must be the *installed*
   copy).

## Known unknowns to validate on-device

* **Info.plist mode dict** — the `ComponentInputModeDict` /
  `tsInputMode*` keys are under-documented; expect one round of fixes
  before the IME shows up in Input Sources.
* **Caret rect** — `attributesForCharacterIndex:lineHeightRectangle:`
  fails in some hosts (wrapped in @try; panel then centres on screen).
  Per-host quirks (Terminal, Electron apps) may need fallbacks.
* **Secure input fields** (passwords) — IMK bypasses IMEs there; just
  confirm no crash.
* **ARC + C++ members** in the controller: `std::unique_ptr` ivars in
  ObjC++ classes are destroyed with the object — already arranged, but
  watch lifetime if you add more.

## Candidate panel v2 (vertical Mongolian, CoreText)

The v1 panel intentionally draws horizontal rows so the whole pipeline is
testable on day one. For v2, reuse the Windows lessons
(README “Vertical Mongolian rendering”):

* Shape the word horizontally (`CTLineCreateWithAttributedString`), then
  rotate the CGContext 90° clockwise per column — CoreText has no native
  TTB-LTR Mongolian layout. Quartz antialiasing is geometric, so rotation
  does not cause the stroke-weight problems Direct2D had.
* Centre each column on the **stem**, not the ink bounding box. Reuse the
  `fontstems.txt` format (`<family>\t<ratio>`); the parser currently
  lives in `src/platform/windows/ui/font_stem_config.cc` — **move it to
  src/core** (it is pure file parsing) as the first step of v2, then both
  platforms share the calibration file format and defaults
  (Noto Sans Mongolian = 0.672 — re-calibrate on macOS, the ratio is
  font-metric-based and should transfer, but verify with a reference
  line).
* Default font “Noto Sans Mongolian” is not preinstalled on macOS; the
  font picker should fall back gracefully (CoreText does automatic
  fallback) and the settings dialog later lets users choose an installed
  Mongolian font (enumerate via CTFontManager, filter by a U+1820 glyph —
  same trick as the Windows picker).

## Settings (later)

Persist the shared `Settings` fields (page size, cloud toggle, UI
language, candidate font) in `NSUserDefaults` under the bundle id, and
build a small settings window using `UiStrings` from `core/localization`
so the three languages come for free. `InputSession::Config::page_size`
is where the page-size setting plugs in.
