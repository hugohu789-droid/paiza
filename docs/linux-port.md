# Linux port guide (IBus, Ubuntu)

Status: engine, INI settings store and GTK settings dialog implemented
and verified in an Ubuntu 24.04 Docker container: clean compile (zero
warnings), 61/61 tests, the settings dialog screenshot-verified under
Xvfb (Mongolian shaping + vertical sample correct), and the engine
registers on a live ibus-daemon (`ibus engine paiza-mongolian` switches
to it). **Never used for real typing on a desktop yet** — that (and the
GNOME Settings flow) is the remaining phase-2 bring-up. The INI settings
store is pure C++ and its unit tests run on every platform.

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
* **The candidate window is drawn by the system panel** (GNOME Shell /
  ibus-ui-gtk3), not by us. v1 therefore uses `IBusLookupTable` with
  vertical orientation: functional (numbers, selection, paging via the
  panel), but each Mongolian word is rendered *horizontally* by the
  panel. True vertical columns are v2 — see below.
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
| candidate window | v1: system IBusLookupTable (vertical list) | **works, not vertical-script** |
| vertical candidate rendering, stem-centred | core/font_stem_config | **TODO v2** (custom GTK window) |
| settings dialog (page size, cloud, font, language, key mapping) | core Settings semantics | wired (GTK3 dialog + INI store) |
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

Phase 3 — vertical candidate window (parity with Windows/macOS):
7. Custom GTK popup (gtk_window_new(GTK_WINDOW_POPUP) on X11; Wayland
   needs layer-shell or the IBus panel protocol — investigate) drawing
   with cairo/Pango: shape each word horizontally, `cairo_rotate(π/2)`
   per column, stem-centred via the shared `fontstems.txt`
   (core/font_stem_config — same calibration data as Windows/macOS).
   Numbers above words, selection pill, footer with ‹ › buttons — port
   of the macOS `candidate_panel.mm` drawing.
8. An offscreen PNG preview harness (cairo image surface, no display
   needed — even easier than the macOS one) to iterate on the visuals in
   CI/containers.

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

`candidate_font` is stored for parity but only takes visual effect with
the v2 custom candidate window (v1's panel uses the system font).
