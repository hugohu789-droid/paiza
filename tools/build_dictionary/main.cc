// Copyright 2026 Paiza. All rights reserved.
//
// Corpus -> dictionary builder. Reads UTF-8 text files containing traditional
// Mongolian (news, essays, ...) and emits, in dictionary.txt format
// (latin_key <TAB> mongolian_utf8 <TAB> frequency), three separate outputs:
//
//   * words     (--out)        : stems, counted on their own
//   * suffixes  (--suffix-out) : affixes, counted separately from the stems
//   * phrases   (--phrase-out) : adjacent stem pairs, in BIGRAM format
//                                (latin1 <TAB> m1 <TAB> latin2 <TAB> m2 <TAB>
//                                weight) — feeds compile_lexicon --bigram.
//
// A token is split at the suffix joiners MVS (U+180E) and NNBSP (U+202F): the
// part before the first joiner is the stem (a word); each part after a joiner
// is a suffix, counted separately and stored WITHOUT the leading joiner (no
// NNBSP-led forms). FVS / NIRUGU stay inside their segment (glyph selectors,
// not suffix boundaries). The latin key is Romanize(segment).
//
// Usage:
//   build_dictionary [--out words.txt] [--suffix-out sfx.txt]
//                    [--phrase-out phr.txt] [--min-freq N] [--phrase-min N]
//                    input1.txt [input2.txt ...]
//   (--min-freq default 1; --phrase-min default 2. Words go to stdout when
//    --out is omitted; suffixes/phrases are written only when their flag is
//    given.)

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "common/string_utils.h"
#include "core/romanization.h"

namespace {

bool IsMongolianLetter(wchar_t cp) {
  return (cp >= 0x1820 && cp <= 0x1878) ||  // Hudum + Todo/Sibe/Manchu letters.
         (cp >= 0x1880 && cp <= 0x18AA);     // Ali Gali.
}

// Word-internal joiners: NIRUGU, FVS1-4 + MVS (U+180A..U+180F), NNBSP. These
// bind a stem and its suffix into one token during tokenisation.
bool IsJoiner(wchar_t cp) {
  return cp == 0x180A || (cp >= 0x180B && cp <= 0x180F) || cp == 0x202F;
}

// Suffix boundaries: only MVS and NNBSP separate a stem from a suffix. FVS /
// NIRUGU are glyph selectors and stay inside a segment.
bool IsSuffixBoundary(wchar_t cp) { return cp == 0x180E || cp == 0x202F; }

// Splits a token into [stem, suffix, suffix, ...] at MVS/NNBSP boundaries,
// dropping the joiners and any empty segments.
std::vector<std::wstring> SplitStemAndSuffixes(const std::wstring& token) {
  std::vector<std::wstring> segments;
  std::wstring current;
  for (wchar_t cp : token) {
    if (IsSuffixBoundary(cp)) {
      if (!current.empty()) segments.push_back(current);
      current.clear();
    } else {
      current.push_back(cp);
    }
  }
  if (!current.empty()) segments.push_back(current);
  return segments;
}

// Splits one line into Mongolian tokens (letters + internal joiners), trimming
// leading/trailing joiners and keeping only tokens that contain a letter.
std::vector<std::wstring> Tokenize(const std::wstring& line) {
  std::vector<std::wstring> tokens;
  std::wstring current;
  bool has_letter = false;
  auto flush = [&] {
    if (has_letter) {
      // Trim leading/trailing joiners.
      size_t b = 0, e = current.size();
      while (b < e && IsJoiner(current[b])) ++b;
      while (e > b && IsJoiner(current[e - 1])) --e;
      if (e > b) tokens.push_back(current.substr(b, e - b));
    }
    current.clear();
    has_letter = false;
  };
  for (wchar_t cp : line) {
    if (IsMongolianLetter(cp)) {
      current.push_back(cp);
      has_letter = true;
    } else if (IsJoiner(cp)) {
      current.push_back(cp);
    } else {
      flush();  // Space / punctuation / digit / latin ends the token.
    }
  }
  flush();
  return tokens;
}

// Romanized lookup key: Romanize strips joiners; also drop any spaces so a
// phrase key is a single typeable string.
std::string KeyFor(const std::wstring& mongolian) {
  std::string key = paiza::Romanize(mongolian);
  key.erase(std::remove(key.begin(), key.end(), ' '), key.end());
  return key;
}

struct Entry {
  std::wstring mongolian;
  uint32_t frequency;
};

std::vector<Entry> SortedByFrequency(
    const std::unordered_map<std::wstring, uint32_t>& counts, uint32_t min) {
  std::vector<Entry> entries;
  for (const auto& [word, freq] : counts) {
    if (freq >= min) entries.push_back({word, freq});
  }
  std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
    if (a.frequency != b.frequency) return a.frequency > b.frequency;
    return a.mongolian < b.mongolian;
  });
  return entries;
}

}  // namespace

// Writes dictionary.txt-format entries to a file (latin<TAB>mong<TAB>freq).
bool WriteDict(const std::string& path, const char* header,
               const std::vector<Entry>& entries) {
  std::ostringstream out;
  out << "# " << header << "\n"
      << "# Format: <latin_key>\\t<mongolian_utf8>\\t<frequency>\n";
  for (const Entry& e : entries) {
    const std::string key = KeyFor(e.mongolian);
    if (key.empty()) continue;
    out << key << '\t' << paiza::WideToUtf8(e.mongolian) << '\t' << e.frequency
        << '\n';
  }
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f.is_open()) return false;
  f << out.str();
  return true;
}

int main(int argc, char** argv) {
  std::string out_path;
  std::string suffix_path;
  std::string phrase_path;
  std::string phrase_entry_path;
  uint32_t min_freq = 1;
  uint32_t phrase_min = 2;
  double phrase_cond = 0.2;  // Min P(word2|word1) for a whole-phrase entry.
  std::vector<std::string> inputs;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--out" && i + 1 < argc) {
      out_path = argv[++i];
    } else if (arg == "--suffix-out" && i + 1 < argc) {
      suffix_path = argv[++i];
    } else if (arg == "--phrase-out" && i + 1 < argc) {
      phrase_path = argv[++i];
    } else if (arg == "--phrase-as-entry" && i + 1 < argc) {
      phrase_entry_path = argv[++i];
    } else if (arg == "--phrase-cond" && i + 1 < argc) {
      phrase_cond = std::atof(argv[++i]);
    } else if (arg == "--min-freq" && i + 1 < argc) {
      min_freq = static_cast<uint32_t>(std::atoi(argv[++i]));
    } else if (arg == "--phrase-min" && i + 1 < argc) {
      phrase_min = static_cast<uint32_t>(std::atoi(argv[++i]));
    } else if (arg.rfind("--", 0) == 0) {
      std::fprintf(stderr, "Unknown flag: %s\n", arg.c_str());
      return 2;
    } else {
      inputs.push_back(arg);
    }
  }
  if (inputs.empty()) {
    std::fprintf(stderr,
                 "Usage: %s [--out w.txt] [--suffix-out s.txt] "
                 "[--phrase-out p.txt] [--min-freq N] [--phrase-min N] "
                 "input.txt ...\n",
                 argv[0]);
    return 2;
  }

  std::unordered_map<std::wstring, uint32_t> words;
  std::unordered_map<std::wstring, uint32_t> suffixes;
  // Phrase bigram key: "stem1\tstem2".
  std::unordered_map<std::wstring, uint32_t> phrases;
  for (const std::string& path : inputs) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
      std::fprintf(stderr, "Cannot open %s\n", path.c_str());
      return 1;
    }
    std::string line;
    while (std::getline(file, line)) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      const std::vector<std::wstring> tokens =
          Tokenize(paiza::Utf8ToWide(line));
      std::vector<std::wstring> stems;  // Per line, for phrase bigrams.
      for (const std::wstring& token : tokens) {
        const std::vector<std::wstring> segments = SplitStemAndSuffixes(token);
        if (segments.empty()) continue;
        words[segments[0]]++;             // Stem.
        stems.push_back(segments[0]);
        for (size_t s = 1; s < segments.size(); ++s) {
          suffixes[segments[s]]++;        // suffix, stored bare.
        }
      }
      if (phrase_min > 0) {
        for (size_t i = 0; i + 1 < stems.size(); ++i) {
          phrases[stems[i] + L'\t' + stems[i + 1]]++;
        }
      }
    }
  }

  // Words -> --out (or stdout).
  const std::vector<Entry> word_entries = SortedByFrequency(words, min_freq);
  if (out_path.empty()) {
    std::ostringstream out;
    for (const Entry& e : word_entries) {
      const std::string key = KeyFor(e.mongolian);
      if (!key.empty())
        out << key << '\t' << paiza::WideToUtf8(e.mongolian) << '\t'
            << e.frequency << '\n';
    }
    std::fputs(out.str().c_str(), stdout);
  } else if (!WriteDict(out_path, "Words (stems) from a Mongolian corpus.",
                        word_entries)) {
    std::fprintf(stderr, "Cannot write %s\n", out_path.c_str());
    return 1;
  }

  // Suffixes -> --suffix-out.
  size_t suffix_count = 0;
  if (!suffix_path.empty()) {
    const std::vector<Entry> suffix_entries =
        SortedByFrequency(suffixes, min_freq);
    suffix_count = suffix_entries.size();
    if (!WriteDict(suffix_path, "Suffixes, counted separately.",
                   suffix_entries)) {
      std::fprintf(stderr, "Cannot write %s\n", suffix_path.c_str());
      return 1;
    }
  }

  // Phrases -> --phrase-out, in compile_lexicon --bigram format.
  size_t phrase_count = 0;
  if (!phrase_path.empty() && phrase_min > 0) {
    std::vector<std::pair<std::wstring, uint32_t>> pairs;
    for (const auto& [key, freq] : phrases) {
      if (freq >= phrase_min) pairs.emplace_back(key, freq);
    }
    std::sort(pairs.begin(), pairs.end(), [](const auto& a, const auto& b) {
      return a.second > b.second;
    });
    std::ostringstream out;
    out << "# Phrase bigrams from a Mongolian corpus.\n"
        << "# Format: latin1\\tm1\\tlatin2\\tm2\\tweight "
           "(feeds compile_lexicon --bigram)\n";
    for (const auto& [key, freq] : pairs) {
      const size_t tab = key.find(L'\t');
      const std::wstring w1 = key.substr(0, tab);
      const std::wstring w2 = key.substr(tab + 1);
      out << KeyFor(w1) << '\t' << paiza::WideToUtf8(w1) << '\t' << KeyFor(w2)
          << '\t' << paiza::WideToUtf8(w2) << '\t' << freq << '\n';
      ++phrase_count;
    }
    std::ofstream f(phrase_path, std::ios::binary | std::ios::trunc);
    if (!f.is_open()) {
      std::fprintf(stderr, "Cannot write %s\n", phrase_path.c_str());
      return 1;
    }
    f << out.str();
  }

  // Whole-phrase candidates -> --phrase-as-entry, dictionary.txt format but
  // keyed on the FIRST word only (so typing it surfaces the phrase), kept
  // only for strong collocations: P(w2|w1) >= cond.
  size_t phrase_entry_count = 0;
  if (!phrase_entry_path.empty() && phrase_min > 0) {
    // (key = Romanize(w1), mongolian = "w1 w2", freq), sorted by freq.
    std::vector<std::pair<std::wstring, uint32_t>> selected;  // w1<TAB>w2.
    for (const auto& [key, freq] : phrases) {
      if (freq < phrase_min) continue;
      const std::wstring w1 = key.substr(0, key.find(L'\t'));
      const uint32_t w1_total = words.count(w1) ? words[w1] : 0;
      if (w1_total == 0) continue;
      if (static_cast<double>(freq) / w1_total < phrase_cond) continue;
      selected.emplace_back(key, freq);
    }
    std::sort(selected.begin(), selected.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    std::ostringstream out;
    out << "# Whole-phrase candidates (keyed on the first word).\n"
        << "# Format: <latin_key>\\t<mongolian_utf8>\\t<frequency>\n";
    for (const auto& [key, freq] : selected) {
      const size_t tab = key.find(L'\t');
      const std::wstring w1 = key.substr(0, tab);
      const std::wstring w2 = key.substr(tab + 1);
      const std::string k = KeyFor(w1);  // First word only.
      if (k.empty()) continue;
      out << k << '\t' << paiza::WideToUtf8(w1 + L' ' + w2) << '\t' << freq
          << '\n';
      ++phrase_entry_count;
    }
    std::ofstream f(phrase_entry_path, std::ios::binary | std::ios::trunc);
    if (!f.is_open()) {
      std::fprintf(stderr, "Cannot write %s\n", phrase_entry_path.c_str());
      return 1;
    }
    f << out.str();
  }

  std::fprintf(stderr,
               "%zu words, %zu suffixes, %zu phrase-bigrams, "
               "%zu phrase-entries written\n",
               word_entries.size(), suffix_count, phrase_count,
               phrase_entry_count);
  return 0;
}
