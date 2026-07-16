# Platform parity — what's left for macOS & Ubuntu

Status snapshot for handing work over to the macOS/Linux front-ends.

## Windows

Complete. No outstanding platform work. (The cloud candidate source is a
cross-platform stub by design, not a Windows gap.)

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

## The two gaps to implement on macOS & Ubuntu — both are settings UI

Windows has these; macOS and Ubuntu don't yet. The core APIs and the
localized strings already exist, so this is UI + settings plumbing only.

### 1. User-dictionary import button

- **Core (ready):** `UserDictionary::ImportFromFile(const std::wstring&)`
  returns `ImportResult{opened, imported, skipped}`. The `UserDictionary*` is
  already created per platform (`g_user_dictionary`).
- **Strings (ready):** `UiStrings::user_dict_label`, `user_dict_import`,
  `user_dict_import_result` (printf `%d`/`%d`), `user_dict_import_failed`.
- **macOS:** add a label + button row in `settings_window.mm`; on click open
  an `NSOpenPanel` (filter `txt`), call `ImportFromFile`, show the result in
  an `NSAlert`. Pass the `UserDictionary*` into the settings window.
- **Ubuntu:** same in `settings_main.cc` with a `GtkFileChooserDialog` +
  `GtkMessageDialog`.
- Reference: Windows `settings_window.cc::ImportUserDictionary` +
  `main.cc` (creates the dictionary, passes it to the settings window).

### 2. Soft-equivalence per-pair checkboxes

Five pairs: `o u`, `q v`, `h g`, `t d`, `E w` (default all on).

- **Settings struct + store:** add five bools (`equiv_ou/qv/hg/td/ew`,
  default true) to the platform `Settings` and persist them
  (macOS `settings_store.mm` NSUserDefaults; Linux `settings_store.cc` INI).
  Windows reference: `service/settings_store.{h,cc}`.
- **Strings (ready):** `UiStrings::section_soft_equiv`, `soft_equiv_hint`.
  The checkbox captions are literal (`o / u`, …), no new strings needed.
- **UI:** a "Soft equivalence" card with five checkboxes
  (macOS `NSButton` set to `NSButtonTypeSwitch`/checkbox; Ubuntu
  `GtkCheckButton`), synced from settings on show, writing back on toggle.
- **Apply to the engine:** rebuild the active equivalence set from the
  enabled pairs and load it into the static lexicon — exactly like Windows
  `main.cc::ApplySettings`:
  ```cpp
  std::string equiv;
  if (s.equiv_ou) equiv += "o u\n";
  if (s.equiv_qv) equiv += "q v\n";
  if (s.equiv_hg) equiv += "h g\n";
  if (s.equiv_td) equiv += "t d\n";
  if (s.equiv_ew) equiv += "E w\n";
  g_static_lexicon->equivalences().LoadFromString(equiv);
  ```
  macOS: in `ApplySettings` (`paiza_input_controller.mm`). Ubuntu: in
  `ApplyCurrentSettings` (`engine.cc`). `g_static_lexicon` exists on both.

## Notes

- Localization: any new labels reuse the existing `UiStrings` fields (en/zh/
  mn-Cyrillic already populated); no `localization.cc` changes expected.
- `localization_test` already covers the relevant fields.
- Optional/deferred everywhere (not a mac/linux-only gap): real cloud source;
  the old→new encoding-standard map for `corpus_normalize`.
