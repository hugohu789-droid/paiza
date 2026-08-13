# Engine Architecture — Class Reference

Design documentation for **`paiza_core`**, the platform-neutral input-method
engine. Everything described here is pure C++20 with no OS dependencies: the
same objects back the Windows (TSF), macOS (InputMethodKit) and Linux (IBus)
front-ends.

**Scope**: the engine only. Platform adapters, UI widgets and settings stores
live under `src/platform/` and are documented in
[platform-parity.md](platform-parity.md), [macos-port.md](macos-port.md) and
[linux-port.md](linux-port.md). Algorithm rationale and the phased build-out
are in [core-engine-spec.md](core-engine-spec.md); the command-line tools are
in [engine-tools.md](engine-tools.md).

---

## 1. Layer map

The engine is five layers. A keystroke enters at the top; a ranked candidate
list comes back out.

```mermaid
flowchart TB
    subgraph L1["① Session layer — key policy"]
        direction LR
        A1["InputSession"]:::s
        A2["KeyMappingStore<br/>KeyMapping"]:::s
        A3["Transliterator<br/>Romanize()"]:::s
    end

    subgraph L2["② Aggregation layer — merge and rank"]
        direction LR
        B1["CandidateEngine"]:::a
        B2{{"«interface»<br/>CandidateSource"}}:::i
    end

    subgraph L3["③ Source layer — interchangeable providers"]
        direction LR
        C1["UserDictionary<br/>priority 200"]:::c
        C2["StaticLexicon<br/>priority 100"]:::c
        C3["LocalDictionarySource<br/>priority 100"]:::c
        C4["CloudCandidateSource<br/>priority 50"]:::c
    end

    subgraph L4["④ Algorithm layer — search and score"]
        direction LR
        D1["DawgView"]:::d
        D2["LevenshteinAutomaton"]:::d
        D3["EquivalenceTable"]:::d
        D4["Scorer"]:::d
        D5["FixedSequenceTable"]:::d
    end

    subgraph L5["⑤ Storage layer — on-disk formats"]
        direction LR
        E1["lexicon.bin<br/>mmap · CRC32 · v4"]:::e
        E2["SQLite<br/>UserHistory · WAL"]:::e
    end

    L1 --> L2
    B1 ==> B2
    C1 & C2 & C3 & C4 -.->|implements| B2
    C2 --> L4
    C2 --> E1
    C1 --> E2

    classDef s fill:#d2e3fc,stroke:#1a73e8,color:#0b1b3a
    classDef a fill:#ceead6,stroke:#188038,color:#0b2a14
    classDef i fill:#e9d2fc,stroke:#a142f4,color:#2a0b3a
    classDef c fill:#fef2c8,stroke:#f29900,color:#3a2a0b
    classDef d fill:#fad2cf,stroke:#c5221f,color:#3a0b0b
    classDef e fill:#e8eaed,stroke:#9aa0a6,color:#202124
```

| Layer | Responsibility | Key types |
|---|---|---|
| ① Session | Key policy state machine, keymap, transliteration | `InputSession`, `KeyMapping`, `KeyMappingStore` |
| ② Aggregation | Poll sources by priority, de-duplicate, cap | `CandidateEngine`, `CandidateSource` |
| ③ Sources | Interchangeable candidate providers | `UserDictionary`, `StaticLexicon`, … |
| ④ Algorithms | Tolerant search, equivalence, ranking, orthography | `DawgView`, `LevenshteinAutomaton`, `Scorer` |
| ⑤ Storage | Compiled binary lexicon, user persistence | `LexiconHeader` family, SQLite |

---

## 2. Class diagrams

Four focused diagrams rather than one dense graph — each covers one layer's
internal structure.

### 2.1 Candidate retrieval (the extensibility core)

The open/closed seam of the whole engine: `CandidateEngine` depends only on the
abstract `CandidateSource`, so a new provider (AI, speech, handwriting) is one
more `AddSource()` call with no change to the pipeline.

```mermaid
classDiagram
    class Candidate {
        +wstring text
        +wstring comment
        +int32_t score
    }

    class CandidateSource {
        <<interface>>
        +name() wstring
        +priority() int
        +IsAvailable() bool
        +Query(latin_input, max_results) vector~Candidate~
    }

    class CandidateEngine {
        -vector~unique_ptr~ sources_
        +AddSource(unique_ptr~CandidateSource~) void
        +Query(latin_input, max_results) vector~Candidate~
        +source_count() size_t
    }

    class UserDictionary {
        -map entries_
        -sqlite3* db_
        -thread writer_
        +name() wstring
        +priority() int
        +Query(latin, max) vector~Candidate~
        +RecordCommit(latin_key, word) void
        +ImportFromFile(path) ImportResult
        +Flush() void
    }

    class StaticLexicon {
        -MappedFile file_
        -LexiconHeader* header_
        -atomic~uint32_t~ context_word_id_
        +Load(bin_path) bool
        +Query(latin, max) vector~Candidate~
        +SetContextWord(word_id) void
        +PredictNext(ctx, max) vector~Candidate~
    }

    class LocalDictionarySource {
        -vector~Entry~ entries_
        +LoadFromFile(path) size_t
        +AddEntry(latin, mongolian, freq) void
        +Query(latin, max) vector~Candidate~
    }

    class CloudCandidateSource {
        -wstring endpoint_url_
        -bool enabled_
        +set_enabled(bool) void
        +Query(latin, max) vector~Candidate~
    }

    UserDictionary ..|> CandidateSource : implements
    StaticLexicon ..|> CandidateSource : implements
    LocalDictionarySource ..|> CandidateSource : implements
    CloudCandidateSource ..|> CandidateSource : implements
    CandidateEngine o-- "1..*" CandidateSource : owns and queries
    CandidateSource ..> Candidate : produces
```

### 2.2 Session layer (the cross-platform parity contract)

`InputSession` holds **non-owning pointers** to everything — all four
collaborators must outlive the session. Its unit tests are the behavioural
contract that keeps the three front-ends identical.
(`KeyMapping::CreateDefault`, `Transliterator::Convert` and
`Transliterator::IsComposableChar` are `static`.)

```mermaid
classDiagram
    class InputSession {
        -CandidateEngine* engine_
        -KeyMappingStore* mappings_
        -UserDictionary* user_dictionary_
        -StaticLexicon* static_lexicon_
        -FixedSequenceTable* fixed_sequences_
        -string latin_buffer_
        -wstring preedit_
        -vector~Candidate~ candidates_
        -size_t selected_
        -size_t page_
        +IsComposableChar(c) bool
        +HandleLetter(letter) KeyResult
        +HandleDigit(digit) KeyResult
        +HandleSpace() KeyResult
        +HandleEnter() KeyResult
        +HandleEscape() KeyResult
        +HandleBackspace() KeyResult
        +HandleOtherKey() KeyResult
        +MoveSelection(delta) void
        +PageForward() bool
        +PageBackward() bool
        +CommitCandidate(index) KeyResult
        +composing() bool
        +preedit() wstring
        +candidates() vector~Candidate~
        +Reset() void
        -RefreshCandidates() void
        -Commit(text) KeyResult
    }

    class Config {
        +size_t page_size
        +size_t max_candidates
    }

    class KeyResult {
        +bool handled
        +optional~wstring~ commit
    }

    class KeyMappingStore {
        -wstring path_
        -KeyMapping mapping_
        -string last_good_content_
        -KeyMapFileStatus file_status_
        -uint64_t revision_
        +Get() KeyMapping
        +ReloadNow() void
        +revision() uint64_t
        +file_status() KeyMapFileStatus
    }

    class KeyMapping {
        -array singles_
        -vector~pair~ sequences_
        +CreateDefault() KeyMapping
        +Convert(latin) wstring
        +Lookup(key) wstring
        +Set(key, value) void
        +LoadFromFile(path) bool
        +SaveToFile(path) bool
        +Entries() vector~pair~
    }

    class KeyMapFileStatus {
        +bool valid
        +size_t error_line
        +string error_text
        +bool restored
    }

    class Transliterator {
        +Convert(latin) wstring
        +IsComposableChar(ch) bool
    }

    InputSession *-- Config : owns
    InputSession ..> KeyResult : returns
    InputSession --> KeyMappingStore : uses
    InputSession --> CandidateEngine : drives
    InputSession --> UserDictionary : records commits
    InputSession --> StaticLexicon : sets bigram context
    InputSession --> FixedSequenceTable : composes MVS
    KeyMappingStore *-- KeyMapping : holds current
    KeyMappingStore *-- KeyMapFileStatus : reports
    Transliterator ..> KeyMapping : facade over default
```

### 2.3 Search and ranking algorithms

The Phase 3 "DFA × FST intersection": `StaticLexicon::IntersectDfs` walks
`DawgView` and `LevenshteinAutomaton` in lock-step, and every surviving
candidate is scored by the shared `Scorer`.

```mermaid
classDiagram
    class StaticLexicon {
        -DawgView dawg_
        -Scorer scorer_
        -EquivalenceTable equivalences_
        -FixedSequenceTable fixed_seq_table_
        +Load(bin_path) bool
        +LoadTuning(scoring, equiv) void
        +WordIdOf(latin, mongolian) uint32_t
        +key_count() uint32_t
        +dawg_node_count() uint32_t
        -LowerBoundKey(s) uint32_t
        -IntersectDfs(node, automaton, state, rank) void
        -SkeletonDfs(input, node, pos, rank, skipped, is_root) void
        -CollectKeyEntries(key_i, edit, variant, skeleton) void
        -BigramRow(context_word_id) vector~uint32_t~
    }

    class DawgView {
        -DawgNode* nodes_
        -uint32_t node_count_
        -DawgTrans* trans_
        -uint32_t trans_count_
        -uint32_t root_
        +valid() bool
        +root() uint32_t
        +node(i) DawgNode
        +trans(i) DawgTrans
    }

    class LevenshteinAutomaton {
        -string query_
        -int max_edits_
        -EquivalenceTable* equivalences_
        +Start() State
        +Step(state, key_char) State
        +CanMatch(state) bool
        +IsAccept(state) bool
        +Distance(state) int
        +max_edits() int
    }

    class State {
        +vector~int~ cur
        +vector~int~ prev
        +char last_char
    }

    class EquivalenceTable {
        -array class_of_
        -int group_count_
        +LoadFromFile(path) int
        +LoadFromString(content) int
        +Equivalent(a, b) bool
        +empty() bool
    }

    class Scorer {
        -ScoringWeights weights_
        +LoadWeights(path) void
        +weights() ScoringWeights
        +Score(signals, now_unix) double
        +ScoreToInt(signals, now_unix) int32_t
    }

    class ScoringWeights {
        +double w_edit
        +double w_variant
        +double w_skeleton
        +double w_phrase
        +double w_bigram
        +double w_sys
        +double w_user
        +double user_halflife_days
    }

    class ScoreSignals {
        +uint32_t system_frequency
        +uint32_t user_frequency
        +int edit_cost
        +int variant_cost
        +int skeleton_cost
        +int phrase_cost
        +bool bigram_hit
        +int64_t last_used_unix
    }

    class FixedSequenceTable {
        -vector~SuffixForm~ suffixes_
        +LoadFromString(text) void
        +InsertSuffixMvs(text) wstring
        +StandardizeNnbspToMvs(token) wstring
        +size() size_t
    }

    StaticLexicon *-- DawgView : owns view
    StaticLexicon *-- Scorer : owns
    StaticLexicon *-- EquivalenceTable : owns
    StaticLexicon *-- FixedSequenceTable : owns
    StaticLexicon ..> LevenshteinAutomaton : constructs per query
    LevenshteinAutomaton *-- State : DP rows
    LevenshteinAutomaton --> EquivalenceTable : free substitutions
    Scorer *-- ScoringWeights : configured by
    Scorer ..> ScoreSignals : consumes
```

### 2.4 Offline compiler and binary layout

`LexiconCompiler` runs at build time only; the structs below are the on-disk
contract shared by writer and mmap reader, pinned by `static_assert` on every
`sizeof`.

```mermaid
classDiagram
    class LexiconCompiler {
        -vector~Entry~ entries_
        -vector~Bigram~ bigrams_
        -string equivalences_
        -string fixed_sequences_
        +AddEntry(latin, mongolian_utf8, frequency) void
        +AddDictionaryFile(path) int
        +AddBigramFile(path) int
        +SetEquivalenceFile(path) bool
        +SetFixedSequenceFile(path) bool
        +Write(path) bool
        +entry_count() size_t
    }

    class LexiconHeader {
        +uint32_t magic
        +uint32_t version
        +uint32_t endian_mark
        +uint32_t payload_crc32
        +uint32_t key_count
        +uint32_t entry_count
        +uint64_t keys_offset
        +uint64_t entries_offset
        +uint64_t dawg_nodes_offset
        +uint64_t equiv_blob_offset
        +uint64_t fixed_blob_offset
        +uint32_t dawg_node_count
        +uint32_t dawg_root
    }

    class KeyRecord {
        +uint32_t key_offset
        +uint32_t key_length
        +uint32_t entry_first
        +uint32_t entry_count
    }

    class EntryRecord {
        +uint32_t text_offset
        +uint32_t text_length
        +uint32_t frequency
        +uint32_t key_index
    }

    class DawgNode {
        +uint32_t trans_first
        +uint32_t trans_count
        +uint32_t word_count
        +uint32_t is_final
    }

    class DawgTrans {
        +uint8_t label
        +uint32_t target
    }

    class BigramEdge {
        +uint32_t word_id2
        +uint32_t weight
    }

    LexiconCompiler ..> LexiconHeader : writes
    LexiconHeader *-- KeyRecord : key_count
    LexiconHeader *-- EntryRecord : entry_count
    LexiconHeader *-- DawgNode : dawg_node_count
    LexiconHeader *-- BigramEdge : bigram_edge_count
    DawgNode *-- DawgTrans : trans_first + count
    KeyRecord --> EntryRecord : entry_first
    StaticLexicon ..> LexiconHeader : mmaps and validates
```

---

## 3. Class reference by module

### 3.1 Session layer

| Class | Header | Responsibility |
|---|---|---|
| `InputSession` | `core/input_session.h` | The complete IME key policy as a testable state machine: letters compose, digits commit, Space/Enter/Escape semantics, candidate paging. **The cross-platform parity contract.** |
| `InputSession::Config` | same | Tuning: `page_size` (digit-selectable range, 1–9), `max_candidates` per keystroke. |
| `InputSession::KeyResult` | same | Return value: `handled` (was the key consumed) + optional `commit` text. |
| `KeyMapping` | `core/key_mapping.h` | Latin key sequence → Mongolian text. Case-sensitive; upper-case = second letter set; longest-sequence-wins (the `ng` digraph). |
| `KeyMappingStore` | `core/key_mapping_store.h` | Hot-reloading provider for `KeyMapping`. Polls the file (500 ms throttle), validates, and **restores the last good content** when an edit is malformed. |
| `KeyMapFileStatus` | same | Validation outcome for the settings UI: `valid`, `error_line`, `error_text`, `restored`. |
| `Transliterator` | `core/transliterator.h` | Static facade over the *default* mapping — the "simple algorithm" fallback when neither the service nor a user keymap is reachable. |
| `Romanize` *(free fn)* | `core/romanization.h` | Mongolian codepoints → canonical Latin key. Defined as the inverse of the default keymap, so dictionary keys are independent of the user's keymap. |

**`InputSession` — main methods**

| Method | Purpose | Notes |
|---|---|---|
| `IsComposableChar(char c)` | Does this key participate in composition? | ASCII letters + any non-digit printable with a keymap entry. Digits never compose. |
| `HandleLetter(char letter)` | Append to the buffer, re-query candidates | Starts a composition when none is active. |
| `HandleDigit(int digit)` | Commit candidate *n* of the current page | `handled=false` when the digit exceeds the page's count (key passes through). |
| `HandleSpace()` / `HandleEnter()` | Commit highlighted candidate / commit raw conversion | Enter commits the transliteration, not a candidate. |
| `HandleEscape()` / `HandleBackspace()` | Cancel / delete last letter | Backspace on an empty buffer cancels. |
| `HandleOtherKey()` | Any other key while composing | Commits the raw conversion and returns `handled=false` so the host still gets the key. |
| `MoveSelection(int delta)` / `PageForward()` / `PageBackward()` | Candidate navigation | Selection wraps; the page follows the selection. |
| `CommitCandidate(size_t index)` | Mouse click on the panel | |
| `preedit()` / `candidates()` / `composing()` | State for the host to render | Host mirrors `preedit()` into marked text. |
| `set_user_dictionary()` / `set_static_lexicon()` / `set_fixed_sequences()` | Wire optional collaborators | **All are non-owning; they must outlive the session.** |

**Host contract** — apply a `KeyResult` in this order:

1. If `commit` is set → insert that text into the client document.
2. If `composing()` → show `preedit()` as marked text and `candidates()` in the panel; otherwise clear marked text and hide the panel.
3. If `handled == false` → let the key fall through to the application.

### 3.2 Aggregation layer

| Class | Header | Responsibility |
|---|---|---|
| `Candidate` | `core/candidate.h` | Value type: `text` (Mongolian, UTF-16), `comment` (source tag), `score`. |
| `CandidateSource` | `core/candidate_source.h` | **Pure-virtual interface** every provider implements. The engine's extension point. |
| `CandidateEngine` | `core/candidate_engine.h` | Owns sources, queries them in priority order, de-duplicates by text (first wins), caps at `max_results`. |

**`CandidateSource` — the four methods to implement**

| Method | Contract |
|---|---|
| `name() → wstring` | Human-readable id, used for logging and the candidate comment tag. |
| `priority() → int` | Larger contributes earlier. Convention: user 200 > lexicon/local 100 > cloud 50. |
| `IsAvailable() → bool` | Unavailable sources are skipped entirely (e.g. cloud while offline). |
| `Query(latin_input, max_results) → vector<Candidate>` | **Must be safe to call from IPC worker threads.** |

> **Adding a provider**: subclass `CandidateSource`, implement the four methods,
> and call `engine.AddSource(std::make_unique<MySource>(...))` during startup.
> No other file changes. `AddSource` is **not** thread-safe against concurrent
> `Query` — register everything before serving keystrokes.

### 3.3 Source layer

| Class | Priority | Backing store | Responsibility |
|---|---|---|---|
| `UserDictionary` | 200 | SQLite (`UserHistory`, WAL) | Dynamic learning layer: frequency boosting, user-coined words, TXT import. In-memory ordered map so prefix search works; **zero I/O on the typing path**. |
| `StaticLexicon` | 100 | `lexicon.bin` (mmap) | Compiled read-only lexicon: exact/prefix/tolerant/skeleton search, bigram prediction. |
| `LocalDictionarySource` | 100 | `dictionary.txt` | Plain-text fallback used when `lexicon.bin` is missing or fails validation. |
| `CloudCandidateSource` | 50 | HTTP endpoint | Stub / extension point; disabled by default. |

**`UserDictionary` — main methods**

| Method | Purpose | Notes |
|---|---|---|
| `UserDictionary(wstring db_path)` | Construct; schema auto-created | Empty path = memory-only (tests). |
| `RecordCommit(latin_key, word)` | Bump frequency, or insert a user-coined entry | Called on every commit; updates memory immediately, schedules async persistence. |
| `ImportFromFile(path) → ImportResult` | Import `latin<TAB>mongolian[<TAB>freq]` | Existing entries keep `max(current, imported)`. Entries may be single letters **or whole phrases**. Returns `{opened, imported, skipped}`. |
| `Flush()` | Block until all pending writes reached SQLite | Tests and shutdown. |

**Threading**: `Query` / `RecordCommit` / `ImportFromFile` are thread-safe (on
Windows the IPC workers query while the settings window imports). A dedicated
writer thread owns the `sqlite3*` handle and batches writes ≥3 s apart; the
destructor flushes synchronously.

**`StaticLexicon` — main methods**

| Method | Purpose | Notes |
|---|---|---|
| `Load(bin_path) → bool` | mmap and validate the compiled lexicon | Checks magic / version / endianness / CRC32. **Returns false and stays unloaded on any failure** — never half-loads, so the engine can fall back to text. |
| `LoadTuning(scoring_path, equivalences_path)` | Optional runtime overrides | Missing files leave the embedded/default values untouched. |
| `Query(latin, max)` | The full search cascade | See §4.1. |
| `SetContextWord(word_id)` / `context_word()` | Bigram context from the last commit | `std::atomic` — on Windows the commit notice and the next query arrive on **different IPC worker threads**. `0` clears. |
| `WordIdOf(latin, mongolian) → uint32_t` | Resolve a committed pair to its id | `0` = unknown. |
| `PredictNext(ctx, max)` | Zero-input suggestion | Highest-weighted successors of the context word. |
| `scorer()` / `equivalences()` | Mutable accessors | How the settings page applies per-pair soft-equivalence toggles at runtime. |
| `fixed_sequence_table()` | Parsed GB/T D.2 suffix table | Handed to `InputSession::set_fixed_sequences`. |

### 3.4 Algorithm layer

| Class | Header | Responsibility |
|---|---|---|
| `BuildDawg` *(free fn)* | `core/dawg.h` | Offline Daciuk incremental minimisation over sorted unique keys → dense node/transition arrays. |
| `DawgView` | `core/dawg.h` | Zero-copy reader over the mmap'd DAWG arrays. |
| `LevenshteinAutomaton` | `core/levenshtein_automaton.h` | Bounded Damerau-Levenshtein (OSA recurrence) over a fixed query; the **DFA half** of the intersection. |
| `LevenshteinAutomaton::State` | same | Two DP rows (`cur`, `prev`) + `last_char`, so the transposition term `d[i-2][j-2]` is available. |
| `EquivalenceTable` | `core/equivalence_table.h` | Data-driven soft-equivalence classes (o/u, q/v, h/g, t/d, E/w) as a 256-entry class map. |
| `Scorer` | `core/scorer.h` | Log-domain linear ranking model; stateless, shared across sources. |
| `FixedSequenceTable` | `core/fixed_sequences.h` | GB/T table D.2 suffix forms: compose-time MVS insertion + vowel harmony. |

**Why the DAWG is "numbered"**: every state stores `word_count`, the number of
keys accepted in its sub-language. Accumulating those counts along a walk yields
the **lexicographic rank** of an accepted key — which *is* its `KeyRecord`
index. That makes the DAWG a minimal perfect hash and removes any need for a
key→id table.

**`LevenshteinAutomaton` — usage pattern**

```cpp
LevenshteinAutomaton automaton(query, /*max_edits=*/1, &equivalences_);
auto state = automaton.Start();
// For each candidate-key character during the DAWG walk:
state = automaton.Step(state, key_char);
if (!automaton.CanMatch(state)) return;      // Prune the whole subtree.
if (automaton.IsAccept(state)) { /* rank == KeyRecord index */ }
```

**`Scorer` — the model**

```
score = w_edit     * (-edit_cost)          // typo distance
      + w_variant  * (-variant_cost)       // soft-equivalence substitutions
      + w_skeleton * (-skeleton_cost)      // vowels omitted
      + w_phrase   * (-phrase_cost)        // extra words in a phrase
      + w_bigram   * (bigram_hit ? 1 : 0)
      + w_sys      * ln(system_frequency + 1)
      + w_user     * ln(decayed_user_frequency + 1)
```

| Weight | Default | Meaning |
|---|---|---|
| `w_edit` | 6.0 | Penalty per edit-distance unit |
| `w_variant` | 2.0 | Per soft-equivalence substitution. **Invariant: `0 < w_variant < w_edit`** — an exact spelling beats its variant, a variant beats a typo |
| `w_skeleton` | 3.0 | Per omitted vowel, so full spellings outrank abbreviations |
| `w_phrase` | 4.0 | Per extra word, so a phrase sits below the single word typed |
| `w_bigram` | 2.0 | Bonus when the previous word predicts this one |
| `w_sys` / `w_user` | 1.0 / 1.2 | Weights on log frequencies |
| `user_halflife_days` | 30.0 | Recency decay; `≤ 0` disables |

All are overridable from `scoring.txt` (`key=value`); missing keys keep their
default.

**`FixedSequenceTable` — two directions**

| Method | Direction | Use |
|---|---|---|
| `InsertSuffixMvs(text)` | compose-time | Longest-match a word-final suffix in the **raw conversion**, insert leading MVS (U+180E), harmonise a/e and u/ü to the stem's gender. |
| `StandardizeNnbspToMvs(token)` | corpus-time | `stem<NNBSP>suffix` → `stem<MVS>suffix` when the suffix is a standard D.2 form. A non-standard NNBSP (genuine enclitic) is left alone. |

> **Honest boundary**: suffix detection is heuristic with no morphological
> analysis — a stem that happens to end in a suffix shape can be mis-detected.
> It therefore only matches forms of length ≥ 2 and only rewrites the raw
> conversion; dictionary candidates are already correctly encoded and are never
> touched.

### 3.5 Storage layer

| Type | Size | Role |
|---|---|---|
| `LexiconHeader` | 160 B | magic `PZLX`, version (**4**), endian mark, payload CRC32, section offsets/counts |
| `KeyRecord` | 16 B | One Latin key: blob offset/length + its entry range |
| `EntryRecord` | 16 B | One word: string-pool offset/length, frequency, owning key |
| `DawgNode` / `DawgTrans` | 16 B / 8 B | Minimal numbered DAWG state / labelled edge |
| `BigramEdge` | 8 B | CSR edge `(word_id2, weight)` |
| `Crc32()` | — | IEEE 802.3 (zlib polynomial), table-driven |

**File layout** (all offsets from file start, little-endian):

```
LexiconHeader
KeyRecord[key_count]           sorted by key bytes
key blob                       concatenated Latin keys, no terminators
EntryRecord[entry_count]       grouped per key, frequency-descending
string pool                    concatenated UTF-8 Mongolian words
uint32[bigram_row_count+1]     CSR row offsets
BigramEdge[bigram_edge_count]  per row, weight-descending
DAWG nodes / transitions       (v2+)
equivalences blob              (v3+) raw equivalences.txt
fixed-sequences blob           (v4+) raw fixed_sequences.txt
```

`word_id` = `EntryRecord` index + 1 (`0` means "no word").

**Design invariants worth knowing**
- Every `sizeof` is pinned by `static_assert` — the layout *is* the contract.
- CRC32 covers everything after the header, so truncation or corruption is
  rejected **wholesale**.
- The equivalence and fixed-sequence rules are embedded **verbatim into the
  binary**, guaranteeing compile-time and query-time agree. This eliminates a
  whole class of config-skew bugs.

### 3.6 FFI boundary

`core/engine_c_api.h` — `extern "C"` for external hosts (C# Avalonia, Qt, …).
The in-repo front-ends bypass this and call C++ directly.

| Function | Purpose |
|---|---|
| `Engine_Initialize(db_path, lexicon_path, csr_path) → bool` | Create the engine; schema auto-created |
| `Engine_Search(latin_input, context_word_id) → const char*` | JSON: `{"candidates":[{"text":…,"source":"user\|lexicon","freq":N}]}` |
| `Engine_CommitCandidate(latin_input, mongolian_utf8)` | Update the user lexicon |
| `Engine_ImportUserDictionary(txt_path_utf8) → int` | Imported count, `-1` if unopenable |
| `Engine_Destroy()` | Flush and release |

**Contract**: not thread-safe — call from a single thread (an IME naturally
does). The string returned by `Engine_Search` is engine-owned and **invalid
after the next `Engine_*` call**; copy it to retain it. All strings are UTF-8.

---

## 4. Runtime behaviour

### 4.1 The search cascade

`StaticLexicon::Query` runs passes in order, stopping early once enough
candidates are collected. Each pass tags its cost, so ranking — not ordering —
decides the final list.

```mermaid
flowchart LR
    Q(["latin_input"]) --> P1["① Exact + prefix<br/>binary search over<br/>sorted KeyRecord table"]
    P1 --> P2["② Tolerant<br/>DAWG × Levenshtein DFS<br/>edit ≤ 1"]
    P2 --> P3["③ Soft equivalence<br/>free substitutions<br/>variant_cost tagged"]
    P3 --> P4["④ Consonant skeleton<br/>vowel-skipping subsequence<br/>lowest priority"]
    P4 --> S["Scorer.ScoreToInt()<br/>+ bigram bonus"]
    S --> R(["ranked candidates"])

    classDef p fill:#d2e3fc,stroke:#1a73e8,color:#0b1b3a
    classDef sc fill:#ceead6,stroke:#188038,color:#0b2a14
    class P1,P2,P3,P4 p
    class S sc
```

| Pass | Mechanism | Cost tagged |
|---|---|---|
| ① Exact / prefix | `LowerBoundKey` binary search | none |
| ② Tolerant | `IntersectDfs` — DAWG × automaton, Damerau ≤ 1 | `edit_cost = 1` |
| ③ Soft equivalence | Free substitutions inside the automaton, matched at distance 0 | `variant_cost` |
| ④ Consonant skeleton | `SkeletonDfs` — input must be a subsequence, omitted chars must all be vowels, **first character never skippable** | `skeleton_cost` |

Because exact matches carry zero cost and every other pass carries a positive
penalty, **an exact spelling always outranks a variant, which always outranks a
typo** — the property that made soft equivalence viable as a scoring tier rather
than a destructive key normalisation.

### 4.2 Dual-key lookup

A user may edit the keymap, but dictionary keys are fixed. Both sides are
therefore indexed and queried twice:

| Time | Key produced |
|---|---|
| Compile | the hand-written gloss key **and** `Romanize(mongolian)` |
| Query | the raw typed Latin **and** `Romanize(keymap.Convert(keys))` |

The results are merged and de-duplicated. This keeps "type the gloss" working
(`mongol` → ᠮᠣᠩᠭᠣᠯ) while a remapped keyboard still lands on the same entry
(`mqnggql` → ᠮᠣᠩᠭᠣᠯ). `Romanize` also strips format characters (BIRGA, NIRUGU,
FVS1-4, MVS, NNBSP) on **both** sides, so a word containing MVS is found without
typing MVS.

### 4.3 Assembling the engine

The order the three front-ends and `paiza_query` all use:

```cpp
// 1. Sources, in priority order.
auto user = std::make_unique<paiza::UserDictionary>(db_path);
auto* user_ptr = user.get();                       // Non-owning, for the session.

auto lexicon = std::make_unique<paiza::StaticLexicon>();
auto* lexicon_ptr = lexicon.get();
if (!lexicon->Load(bin_path)) { /* fall back to LocalDictionarySource */ }
lexicon->LoadTuning(scoring_path, equivalences_path);   // Optional.

// 2. Register with the engine (startup only — not thread-safe vs Query).
paiza::CandidateEngine engine;
engine.AddSource(std::move(user));
engine.AddSource(std::move(lexicon));

// 3. Session, wired to the optional collaborators.
paiza::KeyMappingStore mappings;
paiza::InputSession session(&engine, &mappings);
session.set_user_dictionary(user_ptr);
session.set_static_lexicon(lexicon_ptr);                       // Bigram context.
session.set_fixed_sequences(&lexicon_ptr->fixed_sequence_table());  // MVS compose.

// 4. Drive it from platform key events.
auto result = session.HandleLetter('s');
if (result.commit) InsertIntoDocument(*result.commit);
if (session.composing()) ShowPanel(session.preedit(), session.candidates());
```

**Lifetime rule**: `CandidateEngine` **owns** its sources via
`unique_ptr`; `InputSession` holds **non-owning** pointers to the engine,
keymap store, user dictionary, static lexicon and fixed-sequence table. Every
one of them must outlive the session. Keep the raw pointers *before* moving the
`unique_ptr`s into the engine, as above.

### 4.4 Threading model

| Component | Rule |
|---|---|
| `InputSession` | Single-threaded (the host's input thread). Not thread-safe. |
| `CandidateEngine::Query` | Thread-safe once configuration is done. `AddSource` is startup-only. |
| `CandidateSource::Query` | **Must** be safe to call from IPC worker threads. |
| `UserDictionary` | Fully thread-safe; a dedicated writer thread owns the SQLite handle. |
| `StaticLexicon` | Read-only after `Load`; `context_word_id_` is `std::atomic` because the commit notice and the next query arrive on different threads on Windows. |
| `Scorer` | Stateless after configuration; freely shared. |
| C FFI | Not thread-safe; single-threaded host calls only. |

### 4.5 Data files

| File | Consumed by | Purpose |
|---|---|---|
| `lexicon.bin` | `StaticLexicon` (mmap) | Compiled lexicon: DAWG, entries, bigrams, embedded rules |
| `dictionary.txt` | `LexiconCompiler`, `LocalDictionarySource` | Source lexicon, `latin<TAB>mongolian<TAB>freq` |
| `equivalences.txt` | `LexiconCompiler` → embedded; `EquivalenceTable` | Soft-equivalence groups |
| `fixed_sequences.txt` | `LexiconCompiler` → embedded; `FixedSequenceTable` | GB/T D.2/D.3 fixed sequences |
| `scoring.txt` | `Scorer::LoadWeights` | Weight overrides (`key=value`) |
| `keymap.txt` | `KeyMappingStore` | User keymap; **delta-saved** (only non-default entries) so old files inherit new defaults |
| `user.db` | `UserDictionary` | SQLite `UserHistory`, WAL mode |

---

## 5. Extension points

| I want to… | Do this |
|---|---|
| Add a candidate provider (AI, speech, handwriting) | Subclass `CandidateSource`, implement four methods, `engine.AddSource(...)`. Nothing else changes. |
| Change ranking behaviour | Edit `scoring.txt`, or set `ScoringWeights` via `StaticLexicon::scorer()`. Respect `0 < w_variant < w_edit`. |
| Add/remove a soft-equivalence pair | Edit `data/equivalences.txt` and recompile, or call `equivalences().LoadFromString(...)` at runtime (how the settings checkboxes work). |
| Support a new orthographic fixed sequence | Add a line to `data/fixed_sequences.txt`; the compiler embeds it, no code change. |
| Port to a fourth platform | Implement an adapter that drives `InputSession` and mirrors `preedit()`/`candidates()`. Run `tests/input_session_test.cc` as the parity contract. |
| Embed the engine in a non-C++ host | Use `core/engine_c_api.h`. Mind the string-lifetime and single-thread rules. |

---

## 6. Test coverage map

| Test file | Covers |
|---|---|
| `input_session_test.cc` | **The parity contract** — key policy, paging, commit semantics |
| `static_lexicon_test.cc` | Load/validate, all four search passes, bigram context |
| `dawg_test.cc` | Minimisation, numbering, rank ↔ key index |
| `levenshtein_automaton_test.cc` | OSA recurrence, transposition, pruning, accept |
| `scorer_test.cc` | Weight application, decay, tier ordering |
| `equivalence_table_test.cc` | Group parsing, symmetry, identity fallback |
| `fixed_sequences_test.cc` | MVS insertion, harmony, NNBSP standardisation |
| `romanization_test.cc` | Inverse-of-default property, format stripping, vowel keys |
| `user_dictionary_test.cc` | Learning, persistence, import, flush |
| `key_mapping_test.cc` | Validation, accept/reject/restore, delta-save |
| `candidate_engine_test.cc` | Priority ordering, de-duplication, capping |
| `engine_c_api_test.cc` | FFI lifecycle and JSON shape |
