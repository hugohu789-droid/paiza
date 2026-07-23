// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid
//
// Compose-side use of the GB/T traditional-Mongolian fixed sequences (table
// D.2): case/suffix presentation forms that need a leading MVS (U+180E) to
// render correctly. Parsed from the embedded fixed_sequences.txt text.
//
// InsertSuffixMvs recognises a suffix at the end of a converted codepoint
// string and, when it is not already preceded by an MVS/NNBSP and a stem
// precedes it, inserts the MVS and harmonises the suffix's a/e and u/ü vowels
// to the stem's gender.
//
// This is a heuristic word-final fixup for the RAW conversion (dictionary
// candidates are already correctly encoded). It cannot resolve every
// suffix/stem boundary — a stem that happens to end in a suffix shape may be
// mis-detected — so it deliberately only matches forms of length >= 2.

#ifndef PAIZA_CORE_FIXED_SEQUENCES_H_
#define PAIZA_CORE_FIXED_SEQUENCES_H_

#include <string>
#include <vector>

namespace paiza {

class FixedSequenceTable {
 public:
  // Parses the fixed_sequences.txt text: "MVS h1 h2|h3 ..." lines become
  // suffix forms (each position a set of alternative codepoints). Lines
  // without the MVS marker (collocations) and single-codepoint forms are
  // ignored for MVS insertion.
  void LoadFromString(const std::string& text);

  bool empty() const { return suffixes_.empty(); }
  size_t size() const { return suffixes_.size(); }

  // Returns `text` with a leading MVS inserted before a recognised word-final
  // suffix (longest match) and its harmony vowels set to the stem's gender;
  // returns `text` unchanged when nothing applies.
  std::wstring InsertSuffixMvs(const std::wstring& text) const;

  // Corpus normalisation: if `token` is "stem<NNBSP>suffix" and the suffix
  // exactly matches a standard MVS-form (table D.2), replaces the NNBSP with
  // MVS and harmonises the suffix vowels to the stem's gender. A NNBSP that is
  // not a standard suffix (a genuine enclitic) is left untouched.
  std::wstring StandardizeNnbspToMvs(const std::wstring& token) const;

 private:
  // One suffix form: the codepoints that follow the MVS, each a small set of
  // harmony alternatives.
  using SuffixForm = std::vector<std::vector<wchar_t>>;
  std::vector<SuffixForm> suffixes_;
};

}  // namespace paiza

#endif  // PAIZA_CORE_FIXED_SEQUENCES_H_
