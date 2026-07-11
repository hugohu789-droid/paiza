// Copyright 2026 Paiza. All rights reserved.
//
// Offline lexicon compiler CLI. Turns plain-text dictionary (+ optional
// bigram) sources into the binary lexicon.bin consumed by StaticLexicon.
// Intended to run at build time.
//
// Usage:
//   compile_lexicon --out lexicon.bin --dict dictionary.txt [--dict more.txt]
//                   [--bigram bigrams.txt]

#include <cstdio>
#include <string>
#include <vector>

#include "common/string_utils.h"
#include "core/lexicon_compiler.h"

namespace {

void PrintUsage(const char* argv0) {
  std::fprintf(stderr,
               "Usage: %s --out <lexicon.bin> --dict <file> [--dict <file>...]"
               " [--bigram <file>...]\n",
               argv0);
}

}  // namespace

int main(int argc, char** argv) {
  std::wstring out_path;
  std::vector<std::wstring> dict_paths;
  std::vector<std::wstring> bigram_paths;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto next = [&](const char* flag) -> std::wstring {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "Missing value for %s\n", flag);
        return std::wstring();
      }
      return paiza::Utf8ToWide(argv[++i]);
    };
    if (arg == "--out") {
      out_path = next("--out");
    } else if (arg == "--dict") {
      dict_paths.push_back(next("--dict"));
    } else if (arg == "--bigram") {
      bigram_paths.push_back(next("--bigram"));
    } else {
      std::fprintf(stderr, "Unknown argument: %s\n", arg.c_str());
      PrintUsage(argv[0]);
      return 2;
    }
  }

  if (out_path.empty() || dict_paths.empty()) {
    PrintUsage(argv[0]);
    return 2;
  }

  paiza::LexiconCompiler compiler;
  for (const std::wstring& dict : dict_paths) {
    const int n = compiler.AddDictionaryFile(dict);
    if (n < 0) {
      std::fprintf(stderr, "Cannot open dictionary: %s\n",
                   paiza::WideToUtf8(dict).c_str());
      return 1;
    }
    std::fprintf(stderr, "  dict %s: %d entries\n",
                 paiza::WideToUtf8(dict).c_str(), n);
  }
  for (const std::wstring& bigram : bigram_paths) {
    const int n = compiler.AddBigramFile(bigram);
    if (n < 0) {
      std::fprintf(stderr, "Cannot open bigram file: %s\n",
                   paiza::WideToUtf8(bigram).c_str());
      return 1;
    }
    std::fprintf(stderr, "  bigram %s: %d pairs\n",
                 paiza::WideToUtf8(bigram).c_str(), n);
  }

  if (!compiler.Write(out_path)) {
    std::fprintf(stderr, "Failed to write lexicon: %s\n",
                 paiza::WideToUtf8(out_path).c_str());
    return 1;
  }
  std::fprintf(stderr, "Wrote %s (%zu entries, %zu bigrams)\n",
               paiza::WideToUtf8(out_path).c_str(), compiler.entry_count(),
               compiler.bigram_count());
  return 0;
}
