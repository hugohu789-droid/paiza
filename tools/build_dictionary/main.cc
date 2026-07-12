// Copyright 2026 Paiza. All rights reserved.
//
// Corpus -> dictionary builder. Reads UTF-8 text files containing traditional
// Mongolian (news, essays, ...) and emits a dictionary.txt-format lexicon:
//
//   latin_key <TAB> mongolian_utf8 <TAB> frequency
//
// Tokenisation splits on ASCII whitespace, digits, and punctuation, but keeps
// the Mongolian word-internal joiners together, so an inflected form and its
//附加成分 (case/enclitic suffix), which are joined by MVS (U+180E) or NNBSP
// (U+202F), stay in one entry. Adjacent word pairs above a threshold are
// emitted as phrase entries (Mongolian words separated by a space).
//
// The latin key is Romanize(word) — the project's default-layout romanization
// (format joiners stripped) — so keys match what the engine expects.
//
// Usage:
//   build_dictionary [--out out.txt] [--min-freq N] [--phrase-min N]
//                    input1.txt [input2.txt ...]
//   (--phrase-min 0 disables phrase extraction; default 2. --min-freq
//    default 1. Output goes to stdout when --out is omitted.)

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/string_utils.h"
#include "core/romanization.h"

namespace {

bool IsMongolianLetter(wchar_t cp) {
  return (cp >= 0x1820 && cp <= 0x1878) ||  // Hudum + Todo/Sibe/Manchu letters.
         (cp >= 0x1880 && cp <= 0x18AA);     // Ali Gali.
}

// Word-internal joiners: NIRUGU, FVS1-4 + MVS (U+180A..U+180F), NNBSP. These
// bind a stem and its suffix into one token.
bool IsJoiner(wchar_t cp) {
  return cp == 0x180A || (cp >= 0x180B && cp <= 0x180F) || cp == 0x202F;
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

int main(int argc, char** argv) {
  std::string out_path;
  uint32_t min_freq = 1;
  uint32_t phrase_min = 2;
  std::vector<std::string> inputs;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--out" && i + 1 < argc) {
      out_path = argv[++i];
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
                 "Usage: %s [--out f] [--min-freq N] [--phrase-min N] "
                 "input.txt ...\n",
                 argv[0]);
    return 2;
  }

  std::unordered_map<std::wstring, uint32_t> words;
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
      for (size_t i = 0; i < tokens.size(); ++i) {
        words[tokens[i]]++;
        if (phrase_min > 0 && i + 1 < tokens.size()) {
          phrases[tokens[i] + L" " + tokens[i + 1]]++;
        }
      }
    }
  }

  std::ostringstream out;
  out << "# Generated by build_dictionary from a Mongolian corpus.\n"
      << "# Format: <latin_key>\\t<mongolian_utf8>\\t<frequency>\n";

  const std::vector<Entry> word_entries = SortedByFrequency(words, min_freq);
  for (const Entry& e : word_entries) {
    const std::string key = KeyFor(e.mongolian);
    if (key.empty()) continue;
    out << key << '\t' << paiza::WideToUtf8(e.mongolian) << '\t' << e.frequency
        << '\n';
  }

  size_t phrase_count = 0;
  if (phrase_min > 0) {
    const std::vector<Entry> phrase_entries =
        SortedByFrequency(phrases, phrase_min);
    if (!phrase_entries.empty()) out << "# --- phrases ---\n";
    for (const Entry& e : phrase_entries) {
      const std::string key = KeyFor(e.mongolian);
      if (key.empty()) continue;
      out << key << '\t' << paiza::WideToUtf8(e.mongolian) << '\t'
          << e.frequency << '\n';
      ++phrase_count;
    }
  }

  if (out_path.empty()) {
    std::fputs(out.str().c_str(), stdout);
  } else {
    std::ofstream f(out_path, std::ios::binary | std::ios::trunc);
    if (!f.is_open()) {
      std::fprintf(stderr, "Cannot write %s\n", out_path.c_str());
      return 1;
    }
    f << out.str();
  }
  std::fprintf(stderr, "%zu words, %zu phrases written\n", word_entries.size(),
               phrase_count);
  return 0;
}
