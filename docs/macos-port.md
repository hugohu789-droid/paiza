# macOS port guide (InputMethodKit)

Status: candidate panel v2 (vertical Mongolian) and the settings window
are implemented and were visually verified via an offscreen render
harness on macOS (see the feature-parity table). Still **never run as an
installed input method on a device** — the Info.plist mode dict and caret
rect are the known bring-up risks below.

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
| cloud candidate source (toggle) | CloudCandidateSource | wired (registered on the shared engine, toggled from Settings) |
| vertical candidate rendering, stem-centred | fontstems.txt calibration (core/font_stem_config) | **done** (CoreText columns, 90° CW, stem-centred; paging footer + ‹ › buttons) |
| settings dialog (page size, cloud, font, language, key mapping) | core Settings semantics | **done** (NSUserDefaults store + native AppKit window; clamped to the screen's visible frame — content in an NSScrollView, Save/Close pinned in a footer strip) |
| keymap.txt validation (reject bad edits, restore file, error label) | core ValidateKeyMapFile + KeyMappingStore | wired (1 s refresh timer while the window is open) |
| en/zh/mn-Cyrillic UI strings | core/localization | wired (settings window relabels live) |
| input source name + menu icon | Info.plist + InfoPlist.strings + Resources/MenuIcon.tiff | done ("Paiza Mongolian", black badge / white "MN") |

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
   The system relaunches the process on the next keystroke. To debug,
   attach Xcode/lldb to the running PaizaMongolian process (it must be
   the *installed* copy). Everyday commands: next section.

## Developer commands (build, test, logs, previews)

All commands run from the repo root. 中文速查：构建/测试、日志、设置窗口、
候选窗口的调试命令都在这一节。

### Build + unit tests

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug   # configure once
cmake --build build -j8                        # builds app + tools + tests
ctest --test-dir build --output-on-failure     # 57 portable tests
```

### Install / reinstall the input method

```sh
pkill PaizaMongolian
rm -rf ~/Library/Input\ Methods/PaizaMongolian.app
cp -R build/bin/PaizaMongolian.app ~/Library/Input\ Methods/
```

macOS relaunches the IME on the next keystroke. First-ever install:
log out/in, then System Settings → Keyboard → Input Sources → “+” →
*Paiza Mongolian*.

### Logs (unified log / os_log)

`paiza::Log*` goes to os_log on macOS. **Spell out `/usr/bin/log`** —
zsh has a `log` builtin that silently shadows it and returns nothing.

```sh
# Everything the IME logged in the last 5 minutes:
/usr/bin/log show --last 5m --predicate 'process == "PaizaMongolian"'

# Only Paiza's own lines (init, per-keystroke candidate count/page/caret,
# menu + settings events):
/usr/bin/log show --last 5m --predicate 'eventMessage CONTAINS "PaizaIME"'

# Live, while typing:
/usr/bin/log stream --predicate 'process == "PaizaMongolian"'
```

A healthy keystroke logs e.g.
`sync: 9 candidates, page 1/2 (size 7), caret (612,381 1x18)`.

### Settings window

Normal entry points:
* menu-bar input menu → **Paiza Settings…**;
* double-click `~/Library/Input Methods/PaizaMongolian.app` while the
  IME is running (reopen event).

Test / scripted entry points:

```sh
# Standalone settings window (works even without any client focused;
# a second instance always becomes the settings utility):
~/Library/Input\ Methods/PaizaMongolian.app/Contents/MacOS/PaizaMongolian --settings

# Same, plus a self-rendered PNG of the window (proves in-bundle
# rendering without screen-recording permission):
PAIZA_SETTINGS_SNAPSHOT=/tmp/paiza_settings.png \
  ~/Library/Input\ Methods/PaizaMongolian.app/Contents/MacOS/PaizaMongolian --settings
```

Settings persist in NSUserDefaults (`defaults read
com.paiza.inputmethod.mongolian`); the running IME re-reads them on every
keystroke, so saves apply immediately.

### Candidate window preview (no install needed)

`paiza_panel_preview` renders the real panel offscreen to PNGs — light +
dark mode, and page 2 when the input has more than one page:

```sh
cmake --build build --target paiza_panel_preview
OUT_DIR=/tmp PAIZA_INPUT=sa ./build/bin/paiza_panel_preview
open /tmp/panel_light.png /tmp/panel_dark.png /tmp/panel_page2.png
```

Env vars: `PAIZA_INPUT` — the Latin keys to type (default `sa`, which
yields 9 candidates / 2 pages with the sample dictionary); `PAIZA_DICT` —
dictionary file (default `data/dictionary.txt`).

### Stem-alignment overlay (debug spine)

Draws a red reference line down each column centre of the *installed*
IME's panel, for calibrating `fontstems.txt` ratios per font:

```sh
defaults write com.paiza.inputmethod.mongolian PaizaDebugSpine -bool YES
pkill PaizaMongolian   # relaunches with the overlay on next keystroke
# ... adjust ~/Library/Application\ Support/PaizaIME/fontstems.txt ...
defaults delete com.paiza.inputmethod.mongolian PaizaDebugSpine
pkill PaizaMongolian
```

### Dictionary

`data/dictionary.txt` (bundled to `Contents/Resources/dictionary.txt`) is
a small demo lexicon. Format, one entry per line, `#` for comments:

```
<latin_key><TAB><mongolian_word_utf8><TAB><frequency>
```

Lookup is **prefix-based**, so a fully typed word usually matches only
itself — a single candidate is expected until a real dictionary replaces
the sample. Drop in a bigger file and reinstall to upgrade.

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
* Centre each column on the **stem**, not the ink bounding box. The
  `fontstems.txt` parser now lives in `src/core/font_stem_config.cc`
  (moved out of `src/platform/windows/ui`; the old header is a shim), so
  both platforms share the calibration file format and defaults. The
  macOS panel reads it via `paiza::GetFontStemRatio` and offsets the
  rotated CTLine by `font_size * ratio` (Noto Sans Mongolian = 0.672).
  **Re-calibrate on a device** — verify with the debug spine overlay:
  `defaults write com.paiza.inputmethod.mongolian PaizaDebugSpine -bool YES`.
* Default font “Noto Sans Mongolian” is not preinstalled on macOS; the
  font picker should fall back gracefully (CoreText does automatic
  fallback) and the settings dialog later lets users choose an installed
  Mongolian font (enumerate via CTFontManager, filter by a U+1820 glyph —
  same trick as the Windows picker).

## Settings (done)

The shared `Settings` fields (page size, cloud toggle, UI language,
candidate font) are persisted in `NSUserDefaults` under the bundle id
(`src/platform/macos/settings_store.mm`, key names matching the Windows
registry values). The window (`settings_window.mm`) is a native AppKit
dialog — General card (page-size stepper, cloud NSSwitch, language
NSPopUpButton, font NSPopUpButton with Mongolian-capable fonts floated to
the top and marked with U+1820) and a Key-mapping card (live localized
preview via the shared `core/mapping_preview`, "Edit mapping file…"
opening `keymap.txt`, and a vertical script sample). It is reached from
the input menu's "Paiza Settings…" item and IMK's `showPreferences:`.

The IME is an `LSBackgroundOnly` agent, so the window temporarily switches
the app to `NSApplicationActivationPolicyAccessory` while shown and back
to `Prohibited` on close (no Dock icon).

Settings entry points (all verified on-device):
* menu-bar input menu → **Paiza Settings…** (the IMKInputController menu);
* double-clicking the installed app while the IME runs (reopen event);
* `PaizaMongolian --settings`, or any second instance — a second launch
  becomes a standalone settings window (mirrors `PaizaService --settings`
  on Windows). Settings are re-read from NSUserDefaults on every sync, so
  saves from a standalone instance apply to the running IME on the next
  keystroke.

Note: macOS's own "Open Keyboard Settings…" item (bottom of the input
menu) always opens Apple's System Settings → Keyboard pane; third-party
IMEs cannot add content there. Gotcha discovered on-device: `IMKServer
initWithName:` returns **non-nil even when the connection name is already
registered**, so second-instance detection must use
`NSRunningApplication` (see main.mm), not a nil check.

Diagnostics: `paiza::Log*` goes to os_log on macOS —
`/usr/bin/log show --last 5m --predicate 'eventMessage CONTAINS "PaizaIME"'`
(spell out `/usr/bin/log`; zsh has a `log` builtin that shadows it). The
controller logs candidate count / page / caret rect on every sync.

`InputSession::set_page_size` applies the page-size setting to the live
session; `PaizaCandidatePanel setCandidateFontFamily:` applies the font.
Both are re-applied on every sync, so a Save takes effect immediately on
every open client.
