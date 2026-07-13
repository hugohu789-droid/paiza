// Copyright 2026 Paiza. All rights reserved.
//
// Corpus normaliser (step 1): cleans a UTF-8 Mongolian corpus before
// build_dictionary. Currently it standardises suffix joiners — a token
// "stem<NNBSP>suffix" whose suffix is a standard MVS-form (GB/T table D.2) has
// its NNBSP (U+202F) replaced by MVS (U+180E) and its vowels harmonised to the
// stem's gender. A NNBSP that is not a standard suffix (a genuine enclitic) is
// left untouched.
//
// Homograph review + human-correction application (step 2) are separate flags,
// added later.
//
// Usage:
//   corpus_normalize --out standard.txt [--fixed fixed_sequences.txt]
//                    input1.txt [input2.txt ...]

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "common/string_utils.h"
#include "core/fixed_sequences.h"

namespace {

// Splits a line on ASCII spaces/tabs, keeping the separators out. Mongolian
// word-internal joiners (NNBSP/MVS) are NOT ASCII spaces, so they stay within
// a token.
std::vector<std::wstring> SplitOnSpaces(const std::wstring& line) {
  std::vector<std::wstring> tokens;
  std::wstring current;
  for (wchar_t c : line) {
    if (c == L' ' || c == L'\t') {
      if (!current.empty()) tokens.push_back(current);
      current.clear();
    } else {
      current.push_back(c);
    }
  }
  if (!current.empty()) tokens.push_back(current);
  return tokens;
}

std::string ReadFile(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f.is_open()) return {};
  std::ostringstream buf;
  buf << f.rdbuf();
  return buf.str();
}

}  // namespace

int main(int argc, char** argv) {
  std::string out_path;
  std::string fixed_path = "data/fixed_sequences.txt";
  std::vector<std::string> inputs;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--out" && i + 1 < argc) {
      out_path = argv[++i];
    } else if (arg == "--fixed" && i + 1 < argc) {
      fixed_path = argv[++i];
    } else if (arg.rfind("--", 0) == 0) {
      std::fprintf(stderr, "Unknown flag: %s\n", arg.c_str());
      return 2;
    } else {
      inputs.push_back(arg);
    }
  }
  if (inputs.empty()) {
    std::fprintf(stderr,
                 "Usage: %s --out standard.txt [--fixed fixed_sequences.txt] "
                 "input.txt ...\n",
                 argv[0]);
    return 2;
  }

  paiza::FixedSequenceTable table;
  table.LoadFromString(ReadFile(fixed_path));
  if (table.empty()) {
    std::fprintf(stderr,
                 "No suffix rules loaded from %s; nothing to standardise.\n",
                 fixed_path.c_str());
  }

  std::ostringstream out;
  uint64_t total_tokens = 0;
  uint64_t converted = 0;
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
          SplitOnSpaces(paiza::Utf8ToWide(line));
      std::wstring rebuilt;
      for (size_t i = 0; i < tokens.size(); ++i) {
        ++total_tokens;
        const std::wstring fixed = table.StandardizeNnbspToMvs(tokens[i]);
        if (fixed != tokens[i]) ++converted;
        if (i > 0) rebuilt.push_back(L' ');
        rebuilt += fixed;
      }
      out << paiza::WideToUtf8(rebuilt) << '\n';
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
  std::fprintf(stderr, "%llu tokens, %llu NNBSP->MVS suffix conversions\n",
               static_cast<unsigned long long>(total_tokens),
               static_cast<unsigned long long>(converted));
  return 0;
}
