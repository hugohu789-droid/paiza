# Platform parity — what's left for macOS & Ubuntu

Status snapshot for handing work over to the macOS/Linux front-ends.

## Status: all three platforms at parity

Windows, macOS and Ubuntu now expose the same engine features **and** the
same settings UI (page size, cloud toggle, UI language, candidate font,
key-mapping editor, **user-dictionary import**, **soft-equivalence
per-pair checkboxes**). The two former mac/Linux-only gaps are closed —
see the "DONE" section below. The cloud candidate source is a
cross-platform stub by design, not a platform gap.

## Engine features — already at parity on all three platforms

All engine behaviour lives in `src/core` and is wired on macOS/Linux through
`InputSession` (and on Windows through the TSF text service). No per-platform
work is needed for any of these:

- user dictionary (learning + SQLite persistence) — `set_user_dictionary`
- compiled static lexicon (mmap DAWG, exact/prefix, tolerant Damerau≤1,
  bigram context) — `set_static_lexicon`
- soft equivalence (o/u, q/v, h/g, t/d, E/w) — embedded in `lexicon.bin`
- lossless romanization / dual-key query / format-char stripping — Romanize
- consonant-skeleton input; whole-phrase candidates; bigram next-word
- compose-side MVS insertion + suffix harmony — `set_fixed_sequences`
- keymap delta-save (only non-default keys persisted) — `KeyMapping::SaveToFile`

Candidate-window rendering, key-mapping editing, page size, cloud toggle,
UI language, and candidate font are also at parity. The offline tools
(`compile_lexicon`, `build_dictionary`, `corpus_normalize`, `paiza_query`,
`lexicon_bench`, `tune_scoring`) are portable core-only and build everywhere.
Deployment of `lexicon.bin` already covers the macOS bundle and Linux install.

## The two settings-UI gaps on macOS & Ubuntu — DONE

Both are now implemented on macOS and Ubuntu; all three platforms are at
parity. Verified: macOS full build + 147 tests, Ubuntu container build +
tests + Xvfb screenshot of the dialog showing both new controls.

### 1. User-dictionary import button — done

- Core: `UserDictionary::ImportFromFile` → `ImportResult{opened,imported,
  skipped}`; strings `UiStrings::user_dict_*`.
- **macOS** (`settings_window.mm`): "User dictionary" row in the General
  card with an "Import…" button → `NSOpenPanel` (plain-text UTType filter)
  → `ImportFromFile` → `NSAlert`. The live `g_user_dictionary` is passed
  into the window so imports apply immediately; the standalone `--settings`
  instance (no engine) imports into the shared `user.db` directly.
- **Ubuntu** (`settings_main.cc`): same row + `GtkFileChooserDialog` →
  `GtkMessageDialog`. The dialog is a separate process, so it imports into
  the shared `user.db`; the running engine picks the entries up on its
  next start (`ibus restart`). See docs/linux-port.md.

### 2. Soft-equivalence per-pair checkboxes — done

Five pairs `o u / q v / h g / t d / E w` (default all on).

- Five bools added to the macOS (`settings_store.mm`, NSUserDefaults) and
  Linux (`settings_store.cc`, INI) `Settings`, default true.
- A "Soft equivalence" card with five checkboxes (macOS
  `NSButton checkboxWithTitle:`; Ubuntu `GtkCheckButton`), synced from
  settings and written back on toggle.
- Applied to the engine by rebuilding the equivalence string and calling
  `g_static_lexicon->equivalences().LoadFromString(equiv)` — macOS in
  `ApplySettings` (`paiza_input_controller.mm`), Ubuntu in
  `ApplyCurrentSettings` (`engine.cc`, guarded so it only reloads when a
  pair actually changed).

## Notes

- Localization: any new labels reuse the existing `UiStrings` fields (en/zh/
  mn-Cyrillic already populated); no `localization.cc` changes expected.
- `localization_test` already covers the relevant fields.
- Optional/deferred everywhere (not a mac/linux-only gap): real cloud source;
  the old→new encoding-standard map for `corpus_normalize`.
