# Cross-platform Mongolian Smart IME — Core Engine architecture & implementation spec

Version: v1.1 (engineering revision)
Status: Phase 1, Phase 2 and Phase 3 are all implemented (see §7 milestones).

---

## 1. System Overview

This project builds a cross-platform Mongolian IME core engine that supports a
million-word lexicon, millisecond-scale response, high fault tolerance, and
context-aware prediction.

* **Language**: pure C++ (**C++20**, consistent with the existing repository),
  with zero platform dependencies in the core logic. It exposes a **C-style FFI
  interface** for upper-layer cross-platform UI hosts (C# Avalonia, C++ Qt,
  etc.). The three native front-ends bundled in this repository (Windows TSF /
  macOS IMK / Linux IBus) **keep calling the C++ layer directly through
  `paiza::InputSession`**; the FFI is a wrapper for external hosts, not a
  mandatory path for the in-repo front-ends.
* **Core architecture**: SQLite (offline data management and persistence) + a
  precompiled double-array trie / FST (static base-lexicon lookup) + a
  Levenshtein DFA (fault-tolerant correction) + a CSR sparse matrix (N-gram
  prediction) + an in-memory ordered map (dynamic user layer) — a **dual-track
  overlay**.
* **Relationship to the existing architecture**: the new engine evolves as an
  implementation layer inside the `paiza::CandidateSource` system (the static
  layer replaces `LocalDictionarySource`; the dynamic layer is the new
  `UserDictionary`). `InputSession`, the key policy, and the three front-ends
  **remain unchanged** — this is the repository's established cross-platform
  behavioural-consistency contract.

```
 front-ends (TSF / IMK / IBus / external host via FFI)
        │ keys                     ▲ candidates / commit
        ▼                          │
  InputSession (key policy, platform-neutral)
        ▼
  CandidateEngine (merge / de-dup / rank)
   ├─ UserDictionary        dynamic user layer (memory + async SQLite flush) [Phase 1 ✓]
   ├─ StaticLexicon         static layer (Phase 1: text lexicon; Phase 2: FST+mmap)
   ├─ BigramPredictor       CSR prediction (post-commit)                     [Phase 2]
   └─ CloudCandidateSource  cloud (existing placeholder)
```

## 2. Data-persistence layer design (SQLite schema)

SQLite is used only for **offline lexicon management, TXT import cleanup, and
async persistence of user habits** — it is not on the typing-path real-time
lookup. The database file lives in the user config directory
(`UserConfigDirectory()`; see `common/paths.h` for the per-platform paths).

```sql
-- 1. Core static word table (Unigram). Consumed by the offline compiler to
--    generate the FST/CSR binaries.
CREATE TABLE IF NOT EXISTS Lexicon (
    word_id         INTEGER PRIMARY KEY,
    latin_key       TEXT NOT NULL,      -- normalized Latin encoding
    mongolian_word  TEXT NOT NULL,      -- Mongolian orthographic entity (UTF-8)
    frequency       INTEGER NOT NULL    -- global absolute word frequency
);
CREATE INDEX IF NOT EXISTS idx_latin_key ON Lexicon(latin_key);

-- 2. Context continuation table (Bigram). Compressed offline into CSR.
CREATE TABLE IF NOT EXISTS Bigram (
    word_id_1     INTEGER NOT NULL,
    word_id_2     INTEGER NOT NULL,
    co_frequency  INTEGER NOT NULL,
    PRIMARY KEY (word_id_1, word_id_2)
);
CREATE INDEX IF NOT EXISTS idx_bigram_freq
    ON Bigram(word_id_1, co_frequency DESC);

-- 3. Dynamic user table (UserHistory). The only table written at runtime.
CREATE TABLE IF NOT EXISTS UserHistory (
    latin_key           TEXT NOT NULL,
    mongolian_word      TEXT NOT NULL,
    user_frequency      INTEGER DEFAULT 1,
    last_used_timestamp INTEGER DEFAULT 0,   -- Unix seconds; easy time decay
    PRIMARY KEY (latin_key, mongolian_word)
);
```

Revision notes (relative to v1.0):

* `last_used_timestamp` uses **INTEGER Unix seconds** rather than `DATETIME`
  text — cross-platform comparable, usable in decay math, and free of SQLite's
  implicit text-date conversion traps.
* Every DDL statement uses `IF NOT EXISTS`: the engine builds the schema on
  first launch, with no install step.
* SQLite is **vendored as amalgamation source** (`third_party/sqlite/`,
  public-domain license) for zero external dependency on all three platforms;
  connections open `PRAGMA journal_mode=WAL` so async writes never block
  potential concurrent reads.

## 3. In-memory lookup engine

At runtime the engine uses a **"large static read-only lexicon + small dynamic
read-write user lexicon"** dual-track overlay.

### 3.1 Static read-only layer — Phase 2

The offline compiler reads SQLite and produces a compact binary that is
`mmap`-ed directly at startup:

* **Trie (FST/DAT)**: a million-scale `latin_key → word_id` map, O(k) lookup
  where k is the input length.
* **Prediction matrix (CSR)**: bigrams compressed into two arrays —
  * `Index_Array[word_id_1]` → the row's offset in the data region;
  * `Data_Array`: `(word_id_2, weight)` stored contiguously in descending
    `co_frequency` order.
  * Complexity note: O(1) is the **row lookup**; taking the top k predictions
    is O(k) (the row is already weight-sorted, no extra sort needed).
* **Binary-format requirements** (missing in v1.0, added here): the file header
  carries a magic, format version, byte-order mark, per-section offsets/lengths
  and a CRC32 checksum; on version mismatch or checksum failure the engine
  **refuses to load and falls back** to the text-lexicon path — it never
  half-loads.
* On Windows, mmap maps to `CreateFileMapping/MapViewOfFile`, wrapped as a
  cross-platform utility under `common/`.

Phase 1 transitional implementation: the static layer is temporarily served by
the existing `LocalDictionarySource` (ordered table + binary prefix search),
with the lexicon still a TAB-separated text file. Millisecond response is no
problem up to tens of thousands of words; the interface matches the FST version,
so it can be swapped in place later.

### 3.2 Dynamic user layer — Phase 1 ✓

A lightweight in-memory C++ structure implementing dynamic frequency boosting
and user-coined-word memory, with zero I/O on the typing path:

* **`UserDictionary`** (`core/user_dictionary.h`):
  `std::map<std::string, std::vector<UserWord>>`.
  * Revision: v1.0's `unordered_map` becomes an **ordered `std::map`** — user
    words must also participate in **prefix search** (`lower_bound` range scan),
    which a hash table cannot do; at user-lexicon scale (thousands to tens of
    thousands) the red-black-tree overhead is negligible.
  * `UserWord { mongolian, user_frequency, last_used }`.
* **Lifecycle**: candidate commit → `RecordCommit()` updates the in-memory map
  in real time and marks it dirty → a **dedicated flush thread** (condition-
  variable driven, batched UPSERT, WAL mode) writes incrementally when the
  inter-write interval is ≥3s; a synchronous Flush runs on engine destruction.
  The typing thread never waits on disk.
* **TXT import** (newly required feature): `ImportFromFile(path)` — same format
  as the main lexicon: `latin_key<TAB>mongolian<TAB>frequency(optional)`, UTF-8,
  `#` for comment lines. **Each record may be a single letter or a phrase**: the
  Mongolian field is TAB-delimited and may contain spaces and NNBSP, so phrases
  are naturally supported. Cleaning is line-by-line: invalid lines are skipped
  and counted, returning `{imported, skipped}` for the UI to display; imports
  take effect immediately (write memory + async flush).
* **Commit-notification paths**:
  * macOS / Linux: `InputSession` holds a `UserDictionary*` and records inside
    `Commit` (in-process, naturally real-time).
  * Windows: the engine lives in the service process, so on commit the TSF side
    sends a fire-and-forget `kCommitCandidate` IPC message (an appended message
    type in protocol v1, backward-compatible).

## 4. Decoding pipeline

### 4.1 Core steps

1. **Preprocessing and equivalence-class mapping** — Phase 2
   * The Latin input string is first expanded into a normalized lookup key via
     the equivalence-class table. Equivalence classes are **data, not code**:
     `equivalences.txt` (same directory and same hot-reload mechanism as
     keymap.txt), one `variant char → canonical char` per line. Concrete rules
     like v1.0's "`w` is equivalent to `0x1827`" are **configured in that
     table**, not hardcoded — the key layout has been adjusted several times
     (e.g. the recent o/q/u/v vowel revision), so hardcoding would inevitably go
     stale.
2. **Fault-tolerance space pruning (Levenshtein DFA × FST intersection)** — Phase 2/3
   * An edit-distance ≤1 Levenshtein automaton is built dynamically from the
     input and intersected with the static FST to prune the traversal,
     extracting the set of candidate `word_id`s that allow one extra / missing /
     transposed key.
   * Staged revision: before the lexicon reaches hundreds of thousands of words,
     **Phase 2 first uses "DFS with a tolerance budget + beam pruning over a
     trie"** (simple to implement, equally millisecond-scale); the DFA×FST
     intersection waits until a million-word lexicon and the FST land (Phase 3).
     Both expose the same interface upward.
3. **Dual-track merge** — Phase 1 ✓
   * Static-layer tolerant result set ∪ `UserDictionary` prefix-hit set, with
     same-key de-duplication (existing `CandidateEngine` logic: first-come-
     first-served, ordered by source priority).
4. **Unified scoring and re-ranking** — Phase 1 partial ✓
   * Revised to a **log-domain linear model** (v1.0's formula mixed probability
     and log terms with inconsistent units):

   ```
   Score = w1·log P_err(input | candidate)     — key-error model
         + w2·log P_lm (candidate | context)    — bigram language model
         + w3·log(freq_sys  + 1)                — system frequency (+1 smoothing)
         + w4·log(freq_user + 1)                — user frequency (+1 smoothing)
   ```

### 4.2 Weight strategy

* **P_err (key penalty)**: an exact match has log P_err = 0; each edit
  operation deducts a fixed cost; an equivalence-class hit is **penalty-free**
  (equivalence means the same input intent).
* **P_lm (context gain)**: when the candidate appears in the previous committed
  word's CSR prediction row it gets a strong positive score; outside the row it
  falls back to the unigram (already covered by the w3 term, not double-counted).
* **w4 (strong user-state gain)**: Phase 1 realizes the strong boost via
  **source priority** (UserDictionary priority 200 > static layer 100, so user
  words always rank first); once the Phase 2 scorer lands, this becomes an
  in-formula weight plus a `last_used` time decay (suggested half-life 30 days)
  so a single misselection does not dominate forever.
* The four weights and the decay parameter go into a config file, not hardcoded.

## 5. C++ core interface definition (C-API for FFI)

`core/engine_c_api.h`, exported `extern "C"`. **Memory and threading contract**
(missing in v1.0; the FFI boundary must state it):

* All interfaces are **not thread-safe**; the host must call from a single
  thread (naturally satisfied in an IME scenario); the flush thread is an
  internal engine detail, transparent to the host.
* The string returned by `Engine_Search` is owned by the engine and **becomes
  invalid after the next Engine_ call or Destroy**; the host must copy it to
  retain it. UTF-8 encoded.
* All path parameters are UTF-8.

```c
extern "C" {
  // Engine init. db_path: user-lexicon SQLite (schema self-built if absent).
  // fst_path/csr_path: static binaries; Phase 1 passes a lexicon TXT path / NULL.
  bool Engine_Initialize(const char* db_path,
                         const char* lexicon_path,
                         const char* csr_path);

  // Key lookup. latin_input: pre-normalization Latin string; context_word_id:
  // the previously committed word id (pass 0 with no context; effective in
  // Phase 2). Returns JSON:
  // {"candidates":[{"text":"…","source":"user|lexicon","freq":N},…]}
  const char* Engine_Search(const char* latin_input,
                            uint32_t context_word_id);

  // Commit: update the dynamic user lexicon (in-memory real-time + async flush).
  void Engine_CommitCandidate(const char* latin_input,
                              const char* mongolian_word_utf8);

  // Import a user-lexicon TXT (format per §3.2). Returns the number of imported
  // entries, or -1 if the file cannot be opened.
  int Engine_ImportUserDictionary(const char* txt_path_utf8);

  // Release resources (internally flushes the user lexicon synchronously first).
  void Engine_Destroy();
}
```

Revision note: `Engine_CommitCandidate` drops v1.0's `word_id` parameter — a
user-coined word has no word_id; the dynamic layer's primary key is
`(latin_key, word)`. `Engine_ImportUserDictionary` is added to support the
settings-page import feature.

## 6. Settings page "Import user dictionary" (newly added, Phase 1 ✓)

* All three settings pages get an **Import user dictionary…** button: file
  picker (*.txt) → `UserDictionary::ImportFromFile` → result popup (N imported /
  M lines skipped).
* Windows: the settings window and the engine share the service process, so it
  is a direct in-process call; imports join lookups immediately.
* macOS / Linux: the settings window and engine share a process, same path.
* Copy lives in `core/localization` (en / Chinese / Монгол кирилл, three
  languages).

## 7. Milestones

| Phase | Contents | Status |
|---|---|---|
| **Phase 1** | Vendored SQLite; `UserDictionary` dual-track dynamic layer (memory + async flush + frequency boosting + TXT import); commit notification wired on all three platforms (in-process / IPC); C FFI skeleton (incl. Search/Commit/Import); settings-page import UI; full unit-test coverage | **Implemented** |
| **Phase 2** | Offline compiler (TXT → binary `lexicon.bin`, magic/version/byte-order/CRC header, generated at build time and deployed on all three platforms); `StaticLexicon` mmap static layer (zero-parse startup, binary prefix search); data-driven equivalence-class table; Damerau tolerant search over an implicit trie (edit distance ≤1: insert/delete/substitute + adjacent transposition); log-domain `Scorer` (w1 edit cost + w2 bigram + w3·ln(system freq) + w4·ln(user freq), 30-day half-life time decay, tunable via `scoring.txt`); Bigram/CSR prediction with `SetContextWord`/`PredictNext` (post-commit `context_word_id` effective, wired on all three platforms) | **Implemented** |
| **Phase 3** | Minimal numbered DAWG (prefix+suffix sharing, the number is a minimal perfect hash, rank = KeyRecord index) embedded in `lexicon.bin` (format bumped to v2, generated at build time); explicit Damerau-Levenshtein ≤1 automaton; tolerant search switched to a **joint DAWG × automaton DFS** (replacing Phase 2's implicit-trie DFS + transposition fallback); table-driven CRC32 (large-lexicon startup check ~tens of ms); `lexicon_bench` performance harness + `tune_scoring` weight evaluator | **Implemented** |

Acceptance baseline: every phase keeps the existing GoogleTest suite green + the
three-platform CI passing; as long as the `InputSession` behavioural-contract
tests are unchanged, the front-ends need zero changes.

### Phase 2 implementation landmarks

* Binary format and CRC: `src/core/lexicon_binary.{h,cc}`
* Offline compiler (library + CLI): `src/core/lexicon_compiler.{h,cc}`,
  `tools/compile_lexicon/main.cc` (the build-time `compile_lexicon` target
  generates `lexicon.bin`, deployed alongside each platform's binaries)
* mmap read-only mapping: `src/common/mapped_file.{h,cc}`
  (Windows `MapViewOfFile` / POSIX `mmap`)
* Static-layer lookup + tolerance + bigram: `src/core/static_lexicon.{h,cc}`
* Scorer: `src/core/scorer.{h,cc}`; equivalence classes:
  `src/core/equivalence_table.{h,cc}`
* Commit context: `InputSession::set_static_lexicon` (mac/linux in-process),
  `CandidateService` (Windows `kCommitCandidate` IPC)
* Unit tests: `tests/{static_lexicon,scorer,equivalence_table}_test.cc`

### Input canonicalization — soft equivalence · increment 1

Users may edit the keymap, but the lexicon's `latin_key` is fixed; to decouple
the two and to handle Mongolian same-glyph ambiguity, the "input-canonicalization
layer" is built in increments (design in the project memory):

- **Increment 1 (done)**: soft-equivalence matching. `data/equivalences.txt`
  defines same-glyph / near-sound key pairs (o/u=1825/1826, q/v=1823/1824,
  h/g=182C/182D, t/d=1832/1833); the compiler **embeds them into `lexicon.bin`**
  (format bumped to v3) and `StaticLexicon` reads them from the binary at
  runtime, guaranteeing compile == query consistency. The matching semantics are
  **soft equivalence**, not hard normalization: exact spellings always rank
  first (edit 0 / variant 0), while same-glyph variants are hit at **edit
  distance 0** by the tolerant automaton and tagged with `variant_cost`; the
  scorer applies a `w_variant` penalty (default 2.0, between 0 and w_edit=6),
  ranking them below exact and above true typos. A per-pair toggle can be added
  to the settings page later.
- **Increment 2 (done)**: lossless middle-layer romanization
  (`src/core/romanization`). The romanization table is **the inverse of the
  default keymap** (codepoint → Latin token), so under the default layout
  `Romanize(Convert(x)) == x` (except the single digraph `ng→ᠩ`, normalized to
  `N`). The compiler and `LocalDictionarySource` **append a romanized key**
  (`Romanize(mongolian)`) for every entry containing Mongolian letters, in
  addition to the original hand-typed key. On lookup `InputSession` **queries
  both keys**: the raw keys (hitting the hand-typed / gloss key) + `Romanize(preedit)`
  (hitting the romanized key, layout-independent, digraph-absorbing). This keeps
  the old "type gloss" habit working while letting a remapped keyboard still hit
  the same word. Measured: both `mongol` (gloss) and `mqnggql` (Mongolian
  letters) hit ᠮᠣᠩᠭᠣᠯ.
- **Increment 3 (done)**: format-char stripping + GB/T fixed-sequence embedding.
  `Romanize` now **strips format/control codepoints** from the lookup key (BIRGA
  1800, NIRUGU 180A, FVS1-4 180B/C/D/F, MVS 180E, NNBSP 202F) — both the
  build-time generated key and the runtime query key strip identically, so words
  containing MVS are hit without typing MVS. `data/fixed_sequences.txt` (GB/T
  table D.2's 33 case/suffix fixed sequences + table D.3's 3 fixed collocations;
  format: space-separated hex codepoints, `A|B` harmony variant, `MVS` marker)
  is **embedded into `lexicon.bin` (format bumped to v4)** by the compiler's
  `--fixed`; at runtime `StaticLexicon` loads it and exposes it via
  `fixed_sequences()` for the compose layer (compose-time MVS insertion and
  suffix-harmony normalization land in a later compose increment).
- **Increment 4 (done)**: Windows TSF alignment. The TSF DLL
  (`text_service.cc`) already holds its own `KeyMappingStore` and
  `ConvertBuffer()`; `RefreshCandidates` is changed to the exact same **dual-key
  query** as `InputSession`: raw Latin + `Romanize(ConvertBuffer())` (the
  romanization already includes format-char stripping from increment 3), merged
  and de-duplicated, ranked by score, falling back only when both are empty. The
  three front-ends (TSF / IMK / IBus) now have unified query behaviour, and
  Windows also supports keyboard remapping and format-independent matching.
  Commit still sends the raw Latin (consistent with InputSession, hitting the
  gloss key + static-lexicon context).
- **Increment 5 (done)**: consonant-skeleton input, modeled as a **"vowel-
  skipping subsequence match over the DAWG"** (`StaticLexicon::SkeletonDfs`):
  the input must be a subsequence of the word key, and every skipped key
  character **must be a vowel** (`romanization::IsMongolianVowelKey`, derived
  from the default-layout codepoint 1820–1827 tokens: a e i o q u v E), and
  **the first character cannot be skipped** (implementing "the leading vowel
  cannot be omitted"). It runs as the **lowest-priority pass** (after
  exact/prefix/tolerant, only when those are insufficient), penalized by the
  scorer's `w_skeleton` (default 3.0) by the number of skipped vowels, always
  ranking below exact matches. Measured: `mngl`→ᠮᠣᠩᠭᠣᠯ, `sn`→ᠰᠠᠶᠢᠨ/ᠰᠥᠨᠢ/ᠰᠠᠢᠨ/ᠰᠠᠨᠠᠭᠠ
  (sharing the s-n skeleton, ranked by number of skipped vowels + frequency).
- **Increment 6 (done)**: settings-page soft-equivalence toggles (Windows, one
  checkbox per pair; `ApplySettings` rebuilds the active equivalence set from
  the checkboxes, so unchecking a pair makes it match exactly); compose-side
  **MVS insertion + suffix harmony** (`src/core/fixed_sequences`): parses the
  suffix fixed sequences from the embedded GB/T table D.2, does a longest match
  at the word end against the **raw transliteration** (length ≥2, harmony slot
  `A|B` flexible match, requires a stem, no preceding MVS/NNBSP), and on a hit
  inserts a leading MVS (U+180E) and rewrites the suffix a/e, u/ü by the stem's
  vowel-harmony gender. It acts on the preedit / raw-transliteration commit, not
  on lexicon candidates (the lexicon is already compliant); MVS is stripped from
  the query key by `Romanize`, so lookup is unaffected. Wired on all three
  platforms: `InputSession::set_fixed_sequences` (mac/linux), `TextService`
  (Windows, loading the deployed `fixed_sequences.txt` from the module
  directory). Measured: `abdvr` (masculine stem) → ᠠᠪ᠎ᠳᠤᠷ (MVS inserted);
  `ebdvr` (feminine stem) → ᠡᠪ᠎ᠳᠦᠷ (MVS inserted + u→ü).
  **Honest boundary**: word-final suffix detection is heuristic, with no full
  morphological analysis — a stem that happens to end in a suffix form may be
  misjudged; hence only forms of length ≥2 are matched, and it only acts on the
  raw transliteration (lexicon candidates are unaffected).

### Phase 3 implementation landmarks

* Minimal numbered DAWG (Daciuk incremental minimization + word_count
  numbering): `src/core/dawg.{h,cc}`; on-disk `DawgNode`/`DawgTrans` in
  `lexicon_binary.h` (header bumped to v2, with dawg-section offset/count/root)
* Damerau-Levenshtein ≤1 automaton: `src/core/levenshtein_automaton.{h,cc}`
* Joint DAWG × automaton DFS (rank ↔ KeyRecord index):
  `StaticLexicon::IntersectDfs` (`static_lexicon.cc`)
* Table-driven CRC32: `lexicon_binary.cc`
* Performance harness: `tools/lexicon_bench`; weight evaluator:
  `tools/tune_scoring`; headless candidate-query REPL: `tools/paiza_query`
  (usage in [engine-tools.md](engine-tools.md))
* Unit tests: `tests/{dawg,levenshtein_automaton}_test.cc`
* Measured (Windows Release, random 500k keys / 427k unique keys): DAWG 156k
  nodes (36.5% of the unique keys, 2.7× compression); mmap load ~87ms; query
  P99 ≈ 1.9ms (exact and tolerant are close), meeting P99 < 5ms. Real lexicons
  are smaller with fewer prefix collisions, so latency is lower.
