# Input profiles — supporting multiple script encodings and engines

Design record for making Paiza support more than one Mongolian text encoding and
more than one candidate engine inside a **single codebase**, selected at runtime.

Status: design agreed; increment 1 implemented (see "Rollout" at the end).

---

## 1. Motivation

Paiza is built around Unicode Mongolian: codepoints U+1820..U+18AA, FVS/MVS/NNBSP
format characters, and the GB/T appendix D fixed sequences. That assumption is
baked into several places (`Romanize`, `FixedSequenceTable`, the compiled lexicon,
the corpus tools).

We now need to support additional, non-standard input engines whose text is **not**
standard Unicode Mongolian. The first is **ZCode**, the internal encoding of the
`east-mod/ime` engine: shape codes living in the Unicode **Private Use Area**
(U+E000..U+F8FF), rendered by a matching shape-code (形码) font. A third engine is
anticipated.

Because ZCode values are PUA codepoints, they are still *valid Unicode scalar
values*. That matters enormously: a ZCode string is an ordinary `std::wstring`, so
the TSF / InputMethodKit / IBus commit paths, the candidate window, the key policy
and the platform adapters all carry it unchanged. The problem is therefore
configuration and wiring, not a rewrite.

### Why not a branch

A long-lived branch that changes the core encoding would, in practice, become a
second IME: every fix and every platform feature done twice, a doubled CI matrix,
and eventual unmergeability. Paiza is maintained by one person across three
platforms plus the toolchain. One codebase with a runtime switch is the cheaper
and safer structure.

### Why not an enum

A two-valued `enum { kUnicode, kZCode }` looks tempting, but the thing that varies
between engines is not one flag: it is a **bundle** of about eight settings. With
an enum, every one of them becomes an N-way branch repeated in all three platform
`ApplySettings` functions. Adding a third engine would mean editing the enum, three
settings stores, three `ApplySettings`, and three settings dialogs.

With three concrete cases on the table, generalising is justified. The chosen
design follows the convention already established in this repository: **rules are
data, not code** (`equivalences.txt`, `fixed_sequences.txt`, `keymap.txt`).

---

## 2. The InputProfile concept

An **input profile** is a named bundle of everything that distinguishes one
encoding/engine from another. Profiles are plain text files under `data/profiles/`.

Adding a fourth engine should be *adding a data file*, not editing core code.

### 2.1 What varies between profiles

| Aspect | Unicode | ZCode |
|---|---|---|
| key mapping file | `keymap.txt` (→ U+1820..) | `keymap-zcode.txt` (→ PUA) |
| candidate-window font | Noto Sans Mongolian | the shape-code font |
| compiled lexicon | `lexicon.bin` | `lexicon-zcode.bin` |
| user dictionary | `user.db` | `user-zcode.db` |
| GB/T fixed sequences (MVS insertion + harmony) | yes | no — MVS/FVS do not exist in ZCode |
| romanized second query leg | yes | no — `Romanize` drops PUA codepoints |
| consonant-skeleton input | yes | no initially (needs a ZCode vowel set) |
| vowel key set (for skeleton) | `aeioquvE` | n/a |
| default inline composition | full | hidden (see §5) |

### 2.2 What does *not* change

Worth stating explicitly, because it is most of the system:

- `KeyMapping` is already encoding-agnostic: it maps a key string to an arbitrary
  codepoint sequence. A different `keymap.txt` is all a profile needs.
- `lexicon.bin`'s binary format stores UTF-8 strings and is encoding-agnostic; a
  profile just points at a different file.
- `EquivalenceTable` operates on **Latin keys** (`Equivalent(char, char)`), not on
  Mongolian codepoints, so soft equivalence keeps working in every profile.
- `CandidateSource` / `CandidateEngine`, the candidate window, the settings
  framework, the key policy in `InputSession`, and all three platform adapters are
  untouched.
- The commit path (`ITfRange::SetText`, `insertText:`, `commit_text`) accepts any
  valid Unicode text, PUA included.

### 2.3 File format

Same conventions as the other data files: one `key value` pair per line, `#`
starts a comment, blank lines ignored.

```
# data/profiles/unicode.txt
id               unicode
display_name     Unicode (GB/T)
keymap           keymap.txt
font             Noto Sans Mongolian
lexicon          lexicon.bin
user_db          user.db
fixed_sequences  yes
romanize_query   yes
skeleton_input   yes
vowel_keys       aeioquvE
inline_default   full
```

```
# data/profiles/zcode.txt  (added in a later increment)
id               zcode
display_name     ZCode (shape codes)
keymap           keymap-zcode.txt
font             <shape-code font family>
lexicon          lexicon-zcode.bin
user_db          user-zcode.db
fixed_sequences  no
romanize_query   no
skeleton_input   no
inline_default   hidden
```

### 2.4 Core types

```cpp
struct InputProfile {
  std::string  id;             // Stable key stored in settings.
  std::wstring display_name;   // Shown in the settings dropdown.
  std::wstring keymap, font, lexicon, user_db;   // File names / family name.
  bool fixed_sequences = true;
  bool romanize_query  = true;
  bool skeleton_input  = true;
  std::string  vowel_keys;
  InlineComposition inline_default = InlineComposition::kFull;
};

class InputProfileRegistry {
 public:
  int  LoadFromString(const std::string& content);   // One profile.
  int  LoadFromDirectory(const std::wstring& dir);   // All *.txt; returns count.
  const InputProfile* Find(std::string_view id) const;   // nullptr if absent.
  const std::vector<InputProfile>& All() const;          // For the dropdown.
};
```

Settings store the **string id**, not an enum. A new profile therefore requires no
change to any platform's settings schema.

---

## 3. Switching profiles at runtime

`CandidateEngine::AddSource` is startup-only and not thread-safe against `Query`,
so sources cannot be swapped while running. Instead, **register every profile's
sources at startup and enable only the active one**, reusing the mechanism
`CloudCandidateSource` already has:

```cpp
void set_enabled(bool e) { enabled_ = e; }
bool IsAvailable() const override { return enabled_ && /* existing condition */; }
```

`CandidateEngine::Query` already skips unavailable sources, so a disabled
profile's entries can never leak into the candidate list.

To avoid repeating the N-way switch in three platform files, the logic lives in one
platform-neutral component:

```cpp
class ProfileSwitch {
 public:
  void Register(const std::string& profile_id, CandidateSource* source);
  void Activate(const std::string& profile_id);   // enable/disable + repoint
};
```

Each platform's `ApplySettings` then collapses to one line:

```cpp
profile_switch.Activate(settings.active_profile);
```

Being platform-neutral, `ProfileSwitch` is testable headlessly with GoogleTest,
consistent with this project's preference for mechanically checkable verification
over manual testing on three operating systems.

> `CandidateSource` deliberately does **not** gain a `profile_id()` method. Profile
> membership is a wiring concern; the interface stays at
> `name / priority / IsAvailable / Query`.

### 3.1 Non-owning pointers must be repointed together

`InputSession` holds non-owning pointers for commit recording and bigram context.
On activation, both must be updated or commits will be recorded into the wrong
store:

```cpp
session.set_user_dictionary(active_user_dict);
session.set_static_lexicon(active_lexicon);
session.set_fixed_sequences(profile.fixed_sequences ? &table : nullptr);
```

Note `set_fixed_sequences(nullptr)` already means "no MVS insertion" — disabling
GB/T processing for a profile needs no new code path.

---

## 4. Per-profile user dictionaries

A `user.db` stores `(latin_key, mongolian_word)` pairs. Entries learned under one
encoding are meaningless under another, so **each profile gets its own database
file**; otherwise the candidate list mixes in unreadable codepoints.

Implementation is the `set_enabled` pattern above: register one `UserDictionary`
per profile at startup, enable the active one, and repoint
`InputSession::set_user_dictionary`.

**Known cost at larger N**: each `UserDictionary` owns a SQLite connection and a
writer thread. That is fine for two or three profiles. If the count grows, switch
to lazy opening (create on first activation) or give `UserDictionary` a
`Reopen(path)` so a single instance is re-pointed. Recorded as a TODO rather than
built now.

The same applies to `fontstems.txt` stem calibration, which is per font family and
therefore effectively per profile.

---

## 5. Inline composition toggle

Paiza writes the converted text **inline into the host application's document**,
which the application renders with its own font. In a PUA profile the host's font
will usually lack the shape-code glyphs, so the inline composition shows tofu
boxes. A setting controls this, independently of the profile (profiles only supply
the default):

| Value | Behaviour |
|---|---|
| `full` | Inline shows the converted text (current behaviour, Unicode default) |
| `placeholder` | Inline shows a single placeholder character; the candidate window's preedit strip carries the real content |
| `hidden` | Inline shows nothing |

Applied in the platform adapters, core untouched. On Windows:

```cpp
UpdateComposition(settings.inline_composition == InlineComposition::kFull
                      ? ConvertBuffer() : L"");
```

The commit path is separate (`composition_controller.cc` writes the committed text
through its own `SetText`) and is unaffected.

**Caveat to verify on device**: with an empty composition range,
`ITfContextView::GetTextExt` may return a degenerate rectangle, so candidate-window
positioning falls back to the mouse position. If that proves bad in practice, the
`placeholder` value exists precisely to keep the range measurable while showing
nothing meaningful. (This is the same reasoning behind the placeholder design in
other PUA-era IMEs.)

When inline composition is off, the candidate window's preedit strip is the only
place the user can see what they typed, so it must always be populated —
`SetPreedit` is already called unconditionally.

---

## 6. Costs to keep in view

Each additional engine multiplies, not adds:

- **Licensing**: every engine needs its own compatibility analysis. The ZCode
  engine is a GPL-2.0-or-later port and must therefore run as a **separate process
  over IPC**, never linked into Paiza's AGPL-3.0 core, so that the core remains
  dual-licensable. A third engine must be analysed on its own terms, not assumed
  to match.
- **Test data**: each engine needs its own golden corpus.
- **CI**: the matrix is three platforms times N profiles. At minimum each profile
  should get a `paiza_query` smoke run in CI.

---

## 7. Rollout

| # | Increment | Depends on |
|---|---|---|
| 1 | `InputProfile` + `InputProfileRegistry` + `data/profiles/unicode.txt`. Pure addition: one profile, nothing wired, zero behaviour change. | — |
| 2 | `ProfileSwitch` + unit tests; the three `ApplySettings` collapse to one line. | 1 |
| 3a | `active_profile` in all three settings stores, `data/profiles/` deployed and installed, and a built-in fallback profile so a missing directory is survivable. | 2 |
| 3b | Construct a `ProfileSwitch` in each of the three front-ends and activate from settings (a behaviour-neutral refactor of the existing direct wiring). | 3a |
| 3c | Settings-page profile dropdown built from the registry, on all three platforms. | 3b |
| 4 | `inline_composition` setting (full / placeholder / hidden) on all three platforms. | — |
| 5 | `UserDictionary::set_enabled` plus per-profile registration. | 2 |
| 6 | Add `zcode.txt`, the ZCode keymap and font; verify switching **without code changes**. | 3, 5 |
| 7 | Wire the ZCode engine in as a candidate source (separate process, IPC). | external port |

Increment 3 is split because it is the first one with a behaviour change and it
touches all three platforms: 3a is verifiable locally end to end, 3b is a
refactor whose effect the existing suite already pins, and only 3c adds UI that
cannot be compiled outside its own platform.

Increments 1 to 5 do not depend on the external engine port and can land first.
Every increment must keep the full test suite green and the three-platform CI
passing.
