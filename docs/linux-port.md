# Linux port guide (IBus, Ubuntu)

Status: engine, INI settings store and GTK settings dialog implemented
and verified in an Ubuntu 24.04 Docker container: clean compile (zero
warnings), 61/61 tests, the settings dialog screenshot-verified under
Xvfb (Mongolian shaping + vertical sample correct), and the engine
registers on a live ibus-daemon (`ibus engine paiza-mongolian` switches
to it). Phase-2 on-device finding: the component XML **must** be
installed to ibus's own component dir (see “Component XML location”
below) — with the original `<prefix>/share/ibus/component` rule and the
default `/usr/local` prefix the engine never appeared in any input
source list. Fixed in CMake; verified on a real Ubuntu machine (private
ibus bus: engine listed under *Mongolian*, spawned on demand, switch
OK). Phase 3 (vertical candidates) ships two paths: on Xorg the
engine's own GTK window (spine-verified through the offscreen PNG
preview); on GNOME Wayland the v1 shell popup restyled by the bundled
GNOME Shell extension — drawing our own window over Wayland via
XWayland was tried and reverted after it composited solid black on the
test machine (see "Wayland candidate rendering" below). End-to-end
typing is verified headlessly by `paiza_typing_smoke` (real preedit +
Mongolian commit through a private daemon) and on the desktop via the
GNOME Settings input-source flow. The INI settings store is pure C++
and its unit tests run on every platform.

Framework choice: **IBus** — the default input framework on Ubuntu/GNOME
(a Fcitx5 addon can come later; all IME logic lives in `paiza::InputSession`
so a second Linux front-end is another thin adapter).

## Architecture

```
 host app (gedit, Firefox, ...)
      │  GTK/Qt IM module ↔ ibus-daemon (D-Bus)
      ▼
 ibus-engine-paiza                 our engine process
 ├─ main.cc                        bus/factory/component bootstrap (--ibus)
 ├─ PaizaEngine (engine.cc)        IBusEngine subclass: key events ->
 │                                 InputSession, preedit -> update_preedit,
 │                                 candidates -> IBusLookupTable,
 │                                 panel property "Paiza Settings…"
 ├─ settings_store.{h,cc}          ~/.config/paiza-ime/settings.ini
 │                                 (hot-reloaded, mtime-throttled)
 └─ paiza_core (static lib)        InputSession + CandidateEngine +
                                   LocalDictionarySource + KeyMappingStore +
                                   localization — SHARED with Win/macOS

 paiza-ime-settings (settings_main.cc)   GTK3 dialog, all options; also
                                   registered as the component's <setup>,
                                   so GNOME Settings shows a ⚙ button for
                                   the input source
```

Key differences from Windows/macOS:

* **No service process** (like macOS): the engine process is spawned by
  ibus-daemon and links `CandidateEngine` directly.
* **Two candidate-window paths.** On Xorg the engine draws its own GTK
  popup (v2, `candidate_window.cc` + `candidate_renderer.cc`: true
  vertical columns, stem-centred). On **Wayland** the engine uses the
  v1 `IBusLookupTable` — GNOME Shell's popup, which positions correctly
  for every client — and the bundled **GNOME Shell extension**
  (`gnome-extension/paiza-mongolian@paiza.im`) restyles that popup into
  traditional vertical Mongolian while the Paiza engine is active (see
  "Wayland candidate rendering" below for why the engine must not draw
  its own window there).
* **All key policy lives in `paiza::InputSession`** — the engine must
  stay a dumb translator, same rule as the other platforms.

## Feature-parity contract

| Behaviour | Where defined | Linux status |
|---|---|---|
| a–z / Shift second set (`N`→ᠩ …) | InputSession + KeyMapping | wired |
| keymap.txt hot reload | KeyMappingStore (`~/.config/paiza-ime/keymap.txt`) | wired (same file format) |
| digits 1–9 commit, Space/Enter/Esc/Backspace | InputSession | wired |
| paging (PgUp/PgDn), selection (arrows) | InputSession | wired |
| dictionary + fallback candidate | CandidateEngine (`<prefix>/share/ibus-paiza/dictionary.txt`) | wired |
| cloud candidate source (toggle) | CloudCandidateSource | wired |
| candidate window | Xorg: custom vertical window (v2); Wayland: shell popup (v1) + GNOME extension for vertical words | wired |
| vertical candidate rendering, stem-centred | candidate_renderer.cc + core/font_stem_config (Xorg); shell extension rotation (Wayland, no stem centring — shell font metrics) | wired (spine-verified via PNG preview) |
| settings dialog (page size, cloud, font, language, key mapping) | core Settings semantics | wired (GTK3 dialog + INI store; clamped to the work area, content scrolls, Save/Close pinned) |
| keymap.txt validation (reject bad edits, restore file, error label) | core ValidateKeyMapFile + KeyMappingStore | wired (same on Win/macOS) |
| en/zh/mn-Cyrillic UI strings | core/localization | wired (dialog relabels live) |
| input source name + icon | component XML + PNG icon | "Paiza Mongolian", black MN badge |

## Development steps (the plan)

Phase 1 — engine + settings (this commit):
1. `settings_store.{h,cc}`: `Settings` struct (same fields as
   Windows/macOS), INI persistence, throttled hot reload. Portable C++,
   unit-tested everywhere.
2. `engine.{h,cc}` + `main.cc`: IBusEngine GObject subclass, factory,
   component registration; lookup table synced from the session
   (candidates, cursor = selected index, page size from settings).
3. `settings_main.cc`: GTK3 dialog mirroring the Windows/macOS settings
   windows, localized via `UiStrings`.
4. CMake target (pkg-config `ibus-1.0` / `gtk+-3.0`, auto-skip when the
   dev packages are missing) + component XML + icon + install rules;
   CI ubuntu job installs the dev packages so the front-end compiles.

Phase 2 — bring-up on a real Ubuntu machine (needs a device/VM):
5. Install, register, fix the component/engine metadata until the source
   appears in GNOME Settings (expect one round, like the macOS
   Info.plist did).
6. Validate per-host quirks (Firefox/Electron preedit, Wayland vs X11).

Phase 3 — vertical candidate window (parity with Windows/macOS) — **done
for X11**:
7. `candidate_renderer.{h,cc}` — the cairo/Pango drawing core (port of
   the macOS `candidate_panel.mm`): each word shaped horizontally by
   Pango/HarfBuzz, mapped down the column with the orientation-preserving
   matrix `(0,1,-1,0)` (flow → down, ascent side → right, never
   mirrored), stem-centred via the shared `fontstems.txt`
   (core/font_stem_config — same calibration data and box-widening math
   as Windows/macOS). Numbers above words, selection pill, hover tint,
   footer with `n/m` + ‹ › buttons, same light/dark palettes. Pure
   pangocairo — no GTK window types — so it renders offscreen.
   `candidate_window.{h,cc}` wraps it in a GTK_WINDOW_POPUP
   (override-redirect, never takes focus, RGBA visual for the rounded
   corners) with click-to-commit, hover and page buttons; the engine
   places it right of the caret from `set-cursor-location` and falls
   back to the left/workarea edges like the macOS panel.
   **Engine policy:** `CandidateWindow::Available()` returns true only
   on real Xorg sessions (`WAYLAND_DISPLAY` unset). It pins the X11 GDK
   backend (`g_setenv("GDK_BACKEND","x11")` — a session-inherited
   `GDK_BACKEND` would otherwise veto `gdk_set_allowed_backends`) and
   requires GTK to initialize on an X11 display; headless (the CI
   smoke tests) falls back to the v1 lookup table.

   **Wayland candidate rendering (the black-window lesson).** The
   XWayland route was tried and abandoned: an XWayland
   override-redirect window *maps at the correct position* (verified),
   but on the Ubuntu 24.04 QEMU test machine its contents composite as
   **solid black** — the GTK draw cycle runs (correct clip/CTM/no
   cairo or X errors), and even a raw Xlib+cairo window with a plain
   red fill displays black, while normal managed X11 windows work.
   Root cause presumed in the XWayland/glamor/mutter damage path for
   O-R windows on virtio-gpu; not worth fighting. Readback captures
   (`XGetImage`/`gdk_pixbuf_get_from_window`) proved unreliable for
   diagnosing this (BadMatch/garbage) — only on-screen checks count.
   Instead, Wayland gets the **GNOME Shell extension**
   (`src/platform/linux/gnome-extension/paiza-mongolian@paiza.im`):
   while the current input source is `paiza-mongolian` it rotates each
   candidate label in the shell's own popup 90° clockwise (the same
   transform as our renderers), flows the boxes as left-to-right
   columns, centres the shortcut digit above each word, compacts the
   theme's 12px candidate-box side padding to 3px, and moves the paging
   buttons into a bottom-right footer with a `page/pages` label — the
   Windows/macOS candidate-window arrangement; every other engine sees
   the stock popup. Install to
   `~/.local/share/gnome-shell/extensions/` (or let
   `cmake --install` put it under `<prefix>/share/gnome-shell/extensions`),
   log out/in (Wayland cannot hot-load new extensions), then
   `gnome-extensions enable paiza-mongolian@paiza.im`. Written against
   GNOME Shell 48–50 (`_candidatePopup`/`_candidateArea` internals —
   re-check `ui/ibusCandidatePopup.js` via
   `gresource extract /usr/lib/gnome-shell/libshell-18.so
   /org/gnome/shell/ui/ibusCandidatePopup.js` when a new GNOME
   version lands).
8. `tools/linux/candidate_preview.cc` — offscreen PNG preview harness
   (cairo image surface at 2x, no display needed):
   ```sh
   cmake --build build --target paiza_candidate_preview
   OUT_DIR=/tmp PAIZA_INPUT=sa ./build/bin/paiza_candidate_preview
   ```
   Writes `panel_light.png`, `panel_dark.png`, `panel_page2.png` and
   `panel_spine.png` (debug stem overlay — every stem must sit on the
   red column-centre line; verified with Noto Sans Mongolian, ratio
   0.672). The engine honours `PAIZA_DEBUG_SPINE=1` for the same overlay
   in the live window (parity with Windows `SetDebugSpine` / macOS
   `PaizaDebugSpine`).

## Bring-up steps on Ubuntu

1. Dependencies:
   ```sh
   sudo apt install cmake ninja-build g++ libibus-1.0-dev libgtk-3-dev
   ```
2. Build + portable tests:
   ```sh
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
   cmake --build build -j
   ctest --test-dir build --output-on-failure
   ```
3. Install (default prefix /usr/local):
   ```sh
   sudo cmake --install build
   ibus restart      # or: ibus-daemon -drx
   ```
   Then Settings → Keyboard → Input Sources → “+” → Mongolian →
   *Paiza Mongolian*. The ⚙ button next to the source runs
   `paiza-ime-settings` (the component's `<setup>`).

   **Component XML location (learned the hard way):** ibus-daemon scans
   exactly one directory for component XML — its own
   `<ibus pkgdatadir>/component` (`pkg-config --variable=pkgdatadir
   ibus-1.0` → `/usr/share/ibus/component` on Ubuntu), or the
   `IBUS_COMPONENT_PATH` env var when set (colon-separated; replaces the
   default, so include `/usr/share/ibus/component` too or the daemon
   dies with “Can not execute default config program”, having lost
   dconf). It does **not** scan `<prefix>/share/ibus/component` or
   `~/.local/share/ibus/component` (verified on IBus 1.5.34: the
   registry cache at `~/.cache/ibus/bus/registry` lists the observed
   paths). CMake therefore asks pkg-config for the real directory and
   installs `paiza.xml` there (override with
   `-DPAIZA_IBUS_COMPONENT_DIR=...`); binaries and data stay under the
   prefix, and absolute paths inside the XML make that split fine. If
   the source doesn't show up in `ibus list-engine`, this path is the
   first thing to check.
4. Iterate: after each rebuild,
   ```sh
   sudo cmake --install build && ibus restart
   ```
5. Logs / debugging:
   ```sh
   ibus engine                       # current engine name
   ibus engine paiza-mongolian       # switch from the command line
   journalctl --user -f | grep -i paiza     # engine stderr
   G_MESSAGES_DEBUG=all ibus-daemon -vrx    # verbose ibus in a terminal
   ```
   The engine also honours the shared config files:
   `~/.config/paiza-ime/{settings.ini,keymap.txt,fontstems.txt}`.
6. Settings dialog standalone test (no IME needed):
   ```sh
   ./build/bin/paiza-ime-settings
   ```

## Container verification (no Ubuntu machine needed)

Everything below runs from a macOS/Windows checkout with Docker; the
build tree lands in `build-linux/` (gitignored).

Compile + full test suite:

```sh
docker run --rm -v "$PWD":/src -w /src ubuntu:24.04 bash -c '
  apt-get update -qq && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq \
    cmake g++ make libibus-1.0-dev libgtk-3-dev ca-certificates
  cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Debug
  cmake --build build-linux -j4
  ctest --test-dir build-linux --output-on-failure'
```

Screenshot the settings dialog headlessly (writes
`build-linux/settings_gtk.png`):

```sh
docker run --rm -v "$PWD":/src -w /src ubuntu:24.04 bash -c '
  apt-get update -qq && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq \
    libgtk-3-0t64 libibus-1.0-5 xvfb imagemagick fonts-noto-extra fonts-noto-core dbus
  xvfb-run -a -s "-screen 0 720x900x24" bash -c \
    "./build-linux/bin/paiza-ime-settings & sleep 4; \
     import -window root build-linux/settings_gtk.png; kill %1"'
```

Engine registration smoke test (expects `SWITCH OK`; note: do NOT pass a
custom `--address` to ibus-daemon — clients then cannot discover the
bus and every ibus CLI call hangs):

```sh
docker run --rm -v "$PWD":/src -w /src ubuntu:24.04 bash -c '
  apt-get update -qq && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq \
    ibus dbus-x11 xvfb
  timeout 120 xvfb-run -a bash -c "
    eval \$(dbus-launch --sh-syntax)
    ibus-daemon --daemonize; sleep 3
    ./build-linux/bin/ibus-engine-paiza & sleep 3
    timeout 10 ibus engine paiza-mongolian && echo SWITCH OK
    timeout 5 ibus engine"'
```

(`ibus list-engine` only shows engines installed via component XML, so
the runtime-registered dev engine is absent from the list even though
switching to it works.)

End-to-end typing without any GUI (`paiza_typing_smoke`, an IBus
*client*): on the same private bus as above, with the component XML in
`IBUS_COMPONENT_PATH` (sed the `<exec>` to `build/bin/` to test the
build tree). It switches the **global** engine (the daemon runs in
global-engine mode by default and ignores a per-context SetEngine),
types `PAIZA_INPUT`, commits with Space and requires Mongolian output:

```sh
PAIZA_INPUT=sa ./build-linux/bin/paiza_typing_smoke   # TYPING OK: "ᠰᠠᠶᠢᠨ"
```

No X/Xvfb needed for the daemon or the smoke test; without a display
the engine logs "custom candidate window unavailable" and exercises the
lookup-table fallback.

## Known unknowns to validate on-device

* **candidate_clicked index semantics** — treated as page-relative
  (matching the reference engines); verify and adjust the global-index
  math in `engine.cc` if a panel reports absolute indices.
* **Lookup-table paging vs InputSession paging** — the panel derives the
  page from the cursor position; PgUp/PgDn go through InputSession and
  move the cursor, so both should stay in sync. Watch for panels that
  page independently (`page_up_lookup_table` signals are also wired).
* **Wayland vs X11** — v1 relies entirely on the system panel so it
  should be identical; the v2 custom window will need a Wayland story.
* **GNOME Shell candidate rendering of Mongolian** — expect horizontal
  text with correct shaping (HarfBuzz); confirm the font fallback picks
  a Mongolian-capable font.

## Settings

Same fields and semantics as Windows (registry) and macOS
(NSUserDefaults); Linux persists them as a plain INI, hot-reloaded by the
engine (throttled mtime checks, like keymap.txt):

```
# ~/.config/paiza-ime/settings.ini
candidates_per_page=7
cloud_source_enabled=0
ui_language=0          # 0 en, 1 zh, 2 mn-Cyrillic
candidate_font=Noto Sans Mongolian
```

`candidate_font` takes visual effect in the v2 custom candidate window
(X11); the v1 lookup-table fallback uses the system panel's font.
