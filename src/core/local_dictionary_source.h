// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid
//
// Candidate source backed by a local plain-text dictionary. File format is
// one entry per line: "<latin_key>\t<mongolian_word_utf8>\t<frequency>".
// Lines starting with '#' are comments. Lookup is prefix-based and
// case-sensitive (upper-case keys are the Shift second letter set).

#ifndef PAIZA_CORE_LOCAL_DICTIONARY_SOURCE_H_
#define PAIZA_CORE_LOCAL_DICTIONARY_SOURCE_H_

#include <atomic>
#include <string>
#include <vector>

#include "core/candidate_source.h"

namespace paiza {

class LocalDictionarySource : public CandidateSource {
 public:
  LocalDictionarySource() = default;

  // Loads the dictionary file. Returns the number of entries loaded
  // (0 if the file is missing or empty). Existing entries are replaced.
  size_t LoadFromFile(const std::wstring& path);

  // Adds one entry directly; used by tests and future user-dictionary code.
  void AddEntry(std::string latin_key, std::wstring mongolian, int frequency);

  // CandidateSource:
  std::wstring name() const override { return L"local"; }
  int priority() const override { return 100; }
  // Gated by the active profile, like the other sources: these entries are
  // Unicode Mongolian words, and a profile using another text encoding must
  // not be offered them. Without this they leak into every profile, and in a
  // form-code profile they are not merely wrong but unreadable -- the
  // profile's font has no glyphs for Unicode Mongolian code points, so the
  // host falls back per character and part of the list renders in a
  // different font.
  bool IsAvailable() const override {
    return enabled_.load() && !entries_.empty();
  }
  std::vector<Candidate> Query(const std::string& latin_input,
                               size_t max_results) override;

  void set_enabled(bool enabled) { enabled_.store(enabled); }
  bool enabled() const { return enabled_.load(); }

  size_t entry_count() const { return entries_.size(); }

 private:
  std::atomic<bool> enabled_{true};

  struct Entry {
    std::string latin_key;
    std::wstring mongolian;
    int frequency = 0;
  };

  // Sorts entries by key so Query can binary-search the prefix range.
  void SortEntries();

  std::vector<Entry> entries_;
};

}  // namespace paiza

#endif  // PAIZA_CORE_LOCAL_DICTIONARY_SOURCE_H_
