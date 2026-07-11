// Copyright 2026 Paiza. All rights reserved.
//
// Offline compiler: turns plain-text dictionary + bigram sources into the
// compact binary lexicon (lexicon.bin) consumed by StaticLexicon via mmap.
// Runs at build time (and could run in a settings "rebuild" action); never on
// the typing hot path.

#ifndef PAIZA_CORE_LEXICON_COMPILER_H_
#define PAIZA_CORE_LEXICON_COMPILER_H_

#include <cstdint>
#include <string>
#include <vector>

namespace paiza {

class LexiconCompiler {
 public:
  // One dictionary row: Latin key, Mongolian word (UTF-8), frequency.
  void AddEntry(std::string latin, std::string mongolian_utf8,
                uint32_t frequency);

  // Reads "latin\tmongolian_utf8\tfrequency" lines (frequency optional, '#'
  // comments skipped). Returns the number of rows accepted, or -1 if the file
  // could not be opened.
  int AddDictionaryFile(const std::wstring& path);

  // Reads bigram rows "latin1\tmong1_utf8\tlatin2\tmong2_utf8\tweight". Pairs
  // whose words are not in the dictionary are dropped at Write() time.
  // Returns rows accepted, or -1 if the file could not be opened.
  int AddBigramFile(const std::wstring& path);

  // Builds and atomically writes the binary lexicon. Returns false on I/O
  // failure or when there are no entries.
  bool Write(const std::wstring& path) const;

  size_t entry_count() const { return entries_.size(); }
  size_t bigram_count() const { return bigrams_.size(); }

 private:
  struct Entry {
    std::string latin;
    std::string mongolian;  // UTF-8.
    uint32_t frequency = 0;
  };
  struct Bigram {
    std::string latin1, mongolian1, latin2, mongolian2;
    uint32_t weight = 0;
  };

  std::vector<Entry> entries_;
  std::vector<Bigram> bigrams_;
};

}  // namespace paiza

#endif  // PAIZA_CORE_LEXICON_COMPILER_H_
