# Engine command-line tools

A set of command-line tools that depend on no IME host / GUI, for pure-algorithm
testing, performance acceptance, and scoring tuning of the core engine. This
document focuses on **`paiza_query`** (key sequence → candidate list) and
briefly covers the other tools at the end.

All tools link `paiza_core` and are build-time / development tools
(`EXCLUDE_FROM_ALL`, not produced by the default build). Artifacts land in
`build/bin/<Config>/`.

---

## paiza_query — headless candidate query

Feed a Latin key sequence and get back the engine's candidate list — no IME
framework, no window, pure engine. It runs the same pipeline the three
front-ends use (key mapping → `CandidateEngine` overlaying "user lexicon +
static lexicon", via `InputSession`), so it faithfully reproduces exact/prefix
matching, tolerant matching, user frequency boosting, bigram context, and all
other behaviour.

Source: `tools/paiza_query/main.cc`.

### Build

```
cmake --build build --config Debug --target paiza_query
```

Artifact: `build/bin/Debug/paiza_query` (`paiza_query.exe` on Windows).

> Tip: `lexicon.bin` and `dictionary.txt` are deployed to `build/bin/<Config>/`
> at build time (next to `PaizaService`). Run in that directory and the default
> paths resolve.

### Command-line options

| Option | Meaning | Default |
|---|---|---|
| `--lexicon <bin>` | compiled static lexicon `lexicon.bin` | `lexicon.bin` in the current dir (text lexicon only if absent) |
| `--dict <txt>` | text lexicon `dictionary.txt` (fallback layer) | `dictionary.txt` in the current dir |
| `--keymap <txt>` | key-mapping file `keymap.txt` | built-in default mapping |
| `--user <db>` | user-lexicon SQLite file | empty (in-memory, discarded on exit, reproducible for tests) |
| `<seq> ...` | one or more key sequences to look up directly | none (enters interactive mode) |

### Two run modes

**One-shot (scriptable)**: pass the key sequences as arguments; each is printed
in turn, then it exits.

**Interactive**: run with no key-sequence argument; read sequences line by line
from stdin. REPL commands:

| Command | Action |
|---|---|
| `:N` | commit the Nth candidate from the last result: record it into the user lexicon (frequency/memory) and set it as the bigram context for the next query |
| `:reset` | clear the bigram context |
| `:q` / `:quit` | exit |

### Output format

For each key sequence:
- a `preedit:` line — the key mapping's **direct transliteration** of the
  sequence (the Mongolian preedit string), with `U+XXXX` codepoints in
  parentheses;
- one or more candidate lines — index, Mongolian (UTF-8), `[source]`, `score=`
  composite score, and the `U+XXXX` codepoint string.

Source tags: `[user]` user lexicon (priority 200), `[lexicon]` static lexicon
(100), `[predict]` bigram prediction. The score is the log-domain scorer's
scaled integer; larger is ranked higher. A tolerant hit is pushed down by the
edit cost, possibly to a negative score, so exact matches always precede typos.

### Examples

**1) Exact match** — `sain` returns two spellings, ranked by frequency:

```
$ ./paiza_query.exe sain
> sain
  preedit: ᠰᠠᠢᠨ  (U+1830 U+1820 U+1822 U+1828)
  1. ᠰᠠᠶᠢᠨ  [lexicon]  score=4615  U+1830 U+1820 U+1836 U+1822 U+1828
  2. ᠰᠠᠢᠨ  [lexicon]  score=3714  U+1830 U+1820 U+1822 U+1828
```

**2) Tolerant match** — `mongl` (missing an `o`), `bicig` (missing an `h`) still
hit, but the score is lowered by the edit cost (possibly negative), ranking
after any exact match:

```
$ ./paiza_query.exe mongl bicig
> mongl
  preedit: ᠮᠥᠩᠯ  (U+182E U+1825 U+1829 U+182F)
  1. ᠮᠣᠩᠭᠣᠯ  [lexicon]  score=-1385  U+182E U+1823 U+1829 U+182D U+1823 U+182F
  2. ᠮᠣᠩᠭᠣᠯᠴᠤᠳ  [lexicon]  score=-2566  ...
> bicig
  preedit: ᠪᠢᠴᠢᠭ  (U+182A U+1822 U+1834 U+1822 U+182D)
  1. ᠪᠢᠴᠢᠭ  [lexicon]  score=-1489  U+182A U+1822 U+1834 U+1822 U+182D
```

**3) User-lexicon learning** — in interactive mode, commit a candidate, then
query again to see it rise to the top as `[user]` (higher priority than the
static lexicon):

```
$ printf 'sain\n:2\nsain\n:q\n' | ./paiza_query.exe
> sain
  1. ᠰᠠᠶᠢᠨ  [lexicon]  score=4615  ...
  2. ᠰᠠᠢᠨ  [lexicon]  score=3714  ...
  [committed ᠰᠠᠢᠨ; context set for next query]
> sain
  1. ᠰᠠᠢᠨ  [user]     score=1     U+1830 U+1820 U+1822 U+1828
  2. ᠰᠠᠶᠢᠨ  [lexicon]  score=4615  ...
```

> Bigram context is likewise triggered by `:N`: `SetContextWord` gives that
> word's successor candidates a boost in the next query (provided `lexicon.bin`
> was compiled with bigram data; the sample lexicon has none).

### Windows encoding and font notes

- The tool calls `SetConsoleOutputCP(CP_UTF8)` at startup, so running it
  directly in the console does not produce garbled text.
- **Garbled text ≠ missing glyph**: if it shows `□` boxes (tofu), the current
  console **font lacks Mongolian glyphs** — unrelated to encoding; switch to a
  font with Mongolian coverage (e.g. Noto Sans Mongolian).
- Regardless of font, the trailing **`U+XXXX` codepoints are always readable**
  and are the most reliable basis for pure-algorithm verification.
- Traditional Mongolian is **vertical**; the console shows it horizontally, so
  the direction "not looking like" real Mongolian is normal — the codepoint
  order is the criterion.
- When **capturing/redirecting** output in PowerShell (`> file` or assigning to
  a variable), PowerShell re-decodes it with its own `[Console]::OutputEncoding`,
  which can introduce UTF-16 and similar issues. To see raw bytes, `cmd`
  redirection or a Git Bash pipe is cleanest; viewing interactively in the
  terminal is fine.

### How it works

`paiza_query` assembles the engine in the same order as the real front-ends:

```
CandidateEngine
 ├─ UserDictionary   priority 200   (--user, in-memory by default)
 ├─ StaticLexicon    priority 100   (--lexicon, mmap + DAWG×automaton tolerance)
 └─ LocalDictionary  fallback       (--dict, text lexicon)
```

The key sequence is turned into a preedit string by `KeyMappingStore` (`--keymap`
or the built-in default), then `InputSession` (the cross-platform key-policy
contract, see `src/core/input_session.h`) drives `CandidateEngine::Query`. So
what you see on the command line is exactly what the real IME would produce for
the same keys.

---

## Corpus → lexicon overall pipeline

From a pile of Mongolian articles to a usable `lexicon.bin`, the whole chain
(⚙ = automatic, 🧑 = manual):

```
      Mongolian corpus            data/equivalences.txt   (soft equiv / glyph classes)
      (UTF-8 .txt)                data/fixed_sequences.txt (GB/T D.2 suffixes)
          │                              │  normalize · NNBSP→MVS
          ▼                              ▼
   ┌─────────────────────────────────────────────────┐
   │ ⚙ corpus_normalize  ① analyze                     │
   │   --review review.txt --corrections-skeleton c.tsv│
   └──────────────┬───────────────────┬────────────────┘
                  │                   │
            review.txt            c.tsv (prefilled skeleton)
          (ambiguity groups:            │
           codepoints/romanization/     │
           freq/examples/suggestion) ──► 🧑 delete/edit ─┘  delete=don't fix, edit col 3=change target
                                      │
                                      ▼
   ┌─────────────────────────────────────────────────┐
   │ ⚙ corpus_normalize  ② apply --map c.tsv           │
   │   NNBSP→MVS + whole-word homograph correction     │
   └──────────────┬────────────────────────────────────┘
                  ▼
           standard.txt  (standardized corpus)
                  │
                  ▼
   ┌─────────────────────────────────────────────────┐
   │ ⚙ build_dictionary                                │
   └──┬──────────┬───────────┬───────────┬─────────────┘
      ▼          ▼           ▼           ▼
  dictionary  suffixes    bigrams     phrases
    .txt      (suffixes)  (next-word)  (whole-string cand., key=first word)
      │                      │           │
      └──────────┬───────────┴───────────┘
                 ▼
   ┌─────────────────────────────────────────────────┐
   │ ⚙ compile_lexicon   (auto via cmake --build)      │
   │   --dict dictionary --dict phrases                │
   │   --bigram bigrams --equiv equivalences           │
   │   --fixed fixed_sequences                         │
   └──────────────┬────────────────────────────────────┘
                  ▼
             lexicon.bin  (v4 · mmap · CRC-checked)
                  │  deployed next to binaries (three platforms)
                  ▼
        engine StaticLexicon (Windows TSF / macOS IMK / Linux IBus)
   exact·prefix │ tolerant(edit dist ≤1) │ soft equiv │ consonant skeleton │ whole phrase │ bigram
                                        │
                              compose side: insert MVS + suffix harmony on commit
```

A one-command example (chaining the three steps):

```
corpus_normalize --review review.txt --corrections-skeleton c.tsv \
    --out auto.txt data/corpus/*.txt          # ① analyze (read review.txt, edit c.tsv)
corpus_normalize --map c.tsv --out standard.txt data/corpus/*.txt   # ② apply
build_dictionary --out data/dictionary.txt --suffix-out sfx.txt \
    --phrase-out data/bigrams.txt --phrase-as-entry data/phrases.txt \
    --min-freq 2 --phrase-min 3 --phrase-cond 0.3 standard.txt       # ③ extract lexicon
cmake --build build                            # ④ compile into lexicon.bin and deploy
```

Full options / examples for each tool follow.

## Related tools

| Tool | Purpose | Source |
|---|---|---|
| `compile_lexicon` | offline compiler: `dictionary.txt` (+`bigrams.txt`) → `lexicon.bin` (runs automatically at build time) | `tools/compile_lexicon/main.cc` |
| `build_dictionary` | corpus → `dictionary.txt`: extract words+phrases from Mongolian txt and count frequency | `tools/build_dictionary/main.cc` |
| `corpus_normalize` | corpus cleanup (before `build_dictionary`): standardize NNBSP suffixes to MVS + harmony | `tools/corpus_normalize/main.cc` |
| `lexicon_bench` | performance harness: synthesize a large lexicon, measure P50/P90/P99 query latency and DAWG compression | `tools/lexicon_bench/main.cc` |
| `tune_scoring` | scoring-weight evaluation: report top-1/top-3/MRR on a labelled query set | `tools/tune_scoring/main.cc` |

### build_dictionary — corpus to lexicon (detailed)

Extract **words, suffixes, and phrases** from Mongolian-containing UTF-8 text
(news, essays, etc.) with frequency counts, producing lexicon files in this
project's `dictionary.txt` format (`latin_key<TAB>mongolian<TAB>frequency`).
Source `tools/build_dictionary/main.cc`.

#### Build

```
cmake --build build --config Debug --target build_dictionary
```
Artifact `build/bin/Debug/build_dictionary` (`.exe` on Windows); `EXCLUDE_FROM_ALL`,
a dev tool built on demand.

#### Command-line options

| Option | Meaning | Default |
|---|---|---|
| `<input.txt> ...` | one or more UTF-8 corpus files (positional, required) | — |
| `--out <file>` | **word (stem)** output | prints to stdout by default |
| `--suffix-out <file>` | **suffix** output | not output |
| `--phrase-out <file>` | **phrase bigram** (for prediction) output | not output |
| `--phrase-as-entry <file>` | **whole-string phrase entry** (type the first word to get the whole string) output | not output |
| `--min-freq N` | minimum word/suffix frequency | 1 |
| `--phrase-min N` | minimum phrase co-occurrence count | 2 |
| `--phrase-cond F` | minimum conditional probability P(word2\|word1) for whole-string phrases | 0.2 |

Each item is produced only if its corresponding `--*-out` is given; otherwise it
is skipped.

#### Tokenization and splitting rules

1. **Tokenization**: split on ASCII spaces / digits / punctuation / Latin
   letters; Mongolian punctuation (U+1800–U+1809) also splits.
2. **In-word connectors** stay inside the word for now (no split here): NIRUGU
   (180A), FVS1–4 (180B–180F), MVS (180E), NNBSP (202F).
3. **Stem / suffix split**: split a word into `[stem, suffix, …]` at **MVS
   (U+180E) and NNBSP (U+202F)** — the **first segment = stem** (counted as a
   word), each following segment = **suffix** (counted separately, stored
   **without the leading connector**). FVS/NIRUGU are glyph selectors and are
   **not split points**; they stay in their segment.
4. **latin key**: generated with `Romanize` (inverse of the default layout,
   auto-stripping connectors), consistent with the engine's query key.
5. **Phrases**: bigrams of adjacent **stems** within a line; whole-string
   phrases are further filtered by conditional probability.

#### Four outputs

| Output | Option | Per-line format | Purpose |
|---|---|---|---|
| word (stem) | `--out` | `key<TAB>mongolian<TAB>freq` | lexicon body → `data/dictionary.txt` |
| suffix | `--suffix-out` | same | stats / reference |
| phrase · prediction | `--phrase-out` | `k1<TAB>m1<TAB>k2<TAB>m2<TAB>weight` | feeds `compile_lexicon --bigram`, next-word prediction |
| phrase · whole-string | `--phrase-as-entry` | `key(first word)<TAB>"word1 word2"<TAB>freq` | whole-string candidate → `data/phrases.txt` |

#### Example

Given corpus `news.txt` (shown by codepoint; `⟨NNBSP⟩` = U+202F, invisible):
```
ᠮᠣᠩᠭᠣᠯ ᠤᠯᠤᠰ ᠰᠠᠶᠢᠨ ᠮᠣᠩᠭᠣᠯ ᠤᠯᠤᠰ
ᠮᠣᠩᠭᠣᠯ ᠬᠡᠯᠡ ᠮᠣᠩᠭᠣᠯ ᠬᠡᠯᠡ
ᠤᠯᠤᠰ⟨NNBSP⟩ᠤᠨ ᠤᠯᠤᠰ⟨NNBSP⟩ᠤᠨ        # ᠤᠯᠤᠰ + NNBSP + ᠤᠨ (genitive)
```
Run:
```
build_dictionary --out words.txt --suffix-out sfx.txt \
    --phrase-out bigrams.txt --phrase-as-entry phrases.txt news.txt
```

`words.txt` (stems; the genitive is split off, ᠤᠯᠤᠰ counted 4 times):
```
mqNgql   ᠮᠣᠩᠭᠣᠯ   5
vlvs     ᠤᠯᠤᠰ     4
hele     ᠬᠡᠯᠡ     2
sayin    ᠰᠠᠶᠢᠨ    1
```
`sfx.txt` (suffixes, without the NNBSP prefix):
```
vn   ᠤᠨ   2
```
`bigrams.txt` (prediction):
```
mqNgql  ᠮᠣᠩᠭᠣᠯ  vlvs  ᠤᠯᠤᠰ  2
mqNgql  ᠮᠣᠩᠭᠣᠯ  hele  ᠬᠡᠯᠡ  2
```
`phrases.txt` (whole-string, key = first word; `P(word2|word1)=2/5=0.4 ≥ 0.2`,
so kept):
```
mqNgql   ᠮᠣᠩᠭᠣᠯ ᠤᠯᠤᠰ   2
mqNgql   ᠮᠣᠩᠭᠣᠯ ᠬᠡᠯᠡ   2
```
After placing `phrases.txt` at `data/phrases.txt` and recompiling, typing
`mqNgql` shows:
```
1. ᠮᠣᠩᠭᠣᠯ        [lexicon]   ← the single word first
2. ᠮᠣᠩᠭᠣᠯ ᠤᠯᠤᠰ   [phrase]    ← whole-string candidate, one commit inserts both words
3. ᠮᠣᠩᠭᠣᠯ ᠬᠡᠯᠡ   [phrase]
```

#### Full workflow

```
# 1) corpus → lexicon + phrases (raise the thresholds for a large corpus)
build_dictionary --out data/dictionary.txt --suffix-out sfx.txt \
    --phrase-out data/bigrams.txt --phrase-as-entry data/phrases.txt \
    --min-freq 2 --phrase-min 3 --phrase-cond 0.3  corpus*.txt

# 2) recompile lexicon.bin (data/phrases.txt, data/bigrams.txt embedded at build time)
cmake --build build

# 3) verify: type the first word for whole-string candidates; finish a word for next-word prediction
./build/bin/Debug/paiza_query.exe <keys of the first word>
```

#### Tuning tips

- `--min-freq`: raise (3–5) for a large corpus to filter noise / rare words.
- `--phrase-cond`: raise (0.3–0.5) → keep only the firmest collocations, cleaner
  candidates; lower → broader coverage but noisier.
- `--phrase-min`: minimum phrase co-occurrence; raise for a large corpus.
- Output is the **raw occurrence count**; the engine scores in the log domain,
  so the magnitude does not matter.

#### Caveats

- Input must be **UTF-8** (not GBK/GB2312).
- Whole-string phrase candidates rank **below** same-frequency single words by
  default (`w_phrase` penalty, default 4), at most 3 per query.
- Suffixes are **split out of words, without the leading connector**; the final
  word's suffix is supplied by the user on input, and the compose layer inserts
  MVS automatically (see the "Input canonicalization" section in
  [core-engine-spec.md](core-engine-spec.md)).

### corpus_normalize — corpus cleanup

Clean the corpus before `build_dictionary`. Two things: **NNBSP→MVS suffix
standardization** (always done) + **same-glyph different-encoding correction**
(human-in-the-loop).

**A. NNBSP→MVS (automatic)**: for a word form `stem⟨NNBSP⟩suffix`, if the suffix
is exactly a **D.2 standard MVS suffix form**, replace NNBSP (U+202F) with MVS
(U+180E) and **harmonize** by the stem's gender; NNBSP of a non-standard suffix
(a genuine enclitic) is kept.

**B. Same-glyph correction (three-pass flow)**:
```
# 1) analyze: emit the ambiguity list + a prefilled correction skeleton
corpus_normalize --review review.txt --corrections-skeleton corr.tsv \
    [--out auto.txt] [--review-min 3] corpus.txt ...

# 2) manual: read review.txt, delete/edit in corr.tsv

# 3) apply: automatic NNBSP→MVS + manual corrections → standardized corpus
corpus_normalize --map corr.tsv --out standard.txt corpus.txt ...
```

- **Grouping**: normalize each word by equivalence class (o/u, q/v, h/g, t/d,
  E/w) and strip FVS/NIRUGU; words that are **same-glyph after normalization but
  differ in raw encoding** = one ambiguity group; only list groups with ≥2
  variants and total group frequency ≥ `--review-min` (default 3).
- **`review.txt`** (read-only): for each group, list each variant's
  `freq / U+codepoints / romanization / [SUGGESTED]` (the highest-frequency one
  is the suggested canonical form) + a few example sentences per variant.
- **`corr.tsv`** (skeleton, human-edited): a 4-column TSV
  `wrong_hex ⇥ wrong_mongolian ⇥ right_hex ⇥ right_mongolian`; the machine reads
  columns 1 and 3 (hex authoritative), columns 2 and 4 Mongolian for human
  review. Delete a row = don't correct (different word); edit column 3 = change
  the target.
- **Apply**: **whole-word** match-and-replace + NNBSP→MVS, emitting the
  standardized corpus.

The resulting `standard.txt` then feeds `build_dictionary`. Design details in
the "Input canonicalization" section of [core-engine-spec.md](core-engine-spec.md).

#### Build examples for the remaining tools

```
cmake --build build --config Release --target lexicon_bench
./build/bin/Release/lexicon_bench 500000 30000      # #keys #queries

cmake --build build --config Release --target tune_scoring
./tune_scoring --lexicon lexicon.bin --queries labelled.tsv --scoring scoring.txt
```

Calling `compile_lexicon` directly:

```
./compile_lexicon --out lexicon.bin --dict dictionary.txt [--bigram bigrams.txt]
```

Engine architecture and the phased implementation are detailed in
[core-engine-spec.md](core-engine-spec.md).
