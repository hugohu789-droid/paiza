// Copyright 2026 Paiza. All rights reserved.
//
// Scoring-weight evaluation tool. Runs a labelled query set against a compiled
// lexicon under a given scoring.txt and reports ranking accuracy (top-1,
// top-3, mean reciprocal rank), so weights can be tuned with data instead of
// by guesswork. Point it at different scoring.txt files to compare.
//
// Usage:
//   tune_scoring --lexicon lexicon.bin --queries labelled.tsv
//                [--scoring scoring.txt]
//
// The queries file has one "latin<TAB>expected_mongolian_utf8" pair per line.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "common/string_utils.h"
#include "core/static_lexicon.h"

namespace {

std::wstring Widen(const char* utf8) { return paiza::Utf8ToWide(utf8); }

}  // namespace

int main(int argc, char** argv) {
  std::wstring lexicon_path, queries_path, scoring_path;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--lexicon" && i + 1 < argc) {
      lexicon_path = Widen(argv[++i]);
    } else if (arg == "--queries" && i + 1 < argc) {
      queries_path = Widen(argv[++i]);
    } else if (arg == "--scoring" && i + 1 < argc) {
      scoring_path = Widen(argv[++i]);
    } else {
      std::fprintf(stderr,
                   "Usage: %s --lexicon <bin> --queries <tsv> "
                   "[--scoring <txt>]\n",
                   argv[0]);
      return 2;
    }
  }
  if (lexicon_path.empty() || queries_path.empty()) {
    std::fprintf(stderr, "--lexicon and --queries are required\n");
    return 2;
  }

  paiza::StaticLexicon lex;
  if (!lex.Load(lexicon_path)) {
    std::fprintf(stderr, "Cannot load lexicon\n");
    return 1;
  }
  if (!scoring_path.empty()) lex.LoadTuning(scoring_path, L"");

  std::ifstream file(std::filesystem::path(queries_path), std::ios::binary);
  if (!file.is_open()) {
    std::fprintf(stderr, "Cannot open queries file\n");
    return 1;
  }

  int total = 0, top1 = 0, top3 = 0;
  double mrr = 0.0;
  std::string line;
  while (std::getline(file, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    const size_t tab = line.find('\t');
    if (tab == std::string::npos) continue;
    const std::string latin = line.substr(0, tab);
    const std::wstring expected = paiza::Utf8ToWide(line.substr(tab + 1));
    if (latin.empty() || expected.empty()) continue;

    ++total;
    const auto results = lex.Query(latin, 9);
    for (size_t r = 0; r < results.size(); ++r) {
      if (results[r].text == expected) {
        if (r == 0) ++top1;
        if (r < 3) ++top3;
        mrr += 1.0 / static_cast<double>(r + 1);
        break;
      }
    }
  }

  if (total == 0) {
    std::fprintf(stderr, "No queries evaluated\n");
    return 1;
  }
  std::printf("queries=%d  top1=%.1f%%  top3=%.1f%%  MRR=%.3f\n", total,
              100.0 * top1 / total, 100.0 * top3 / total, mrr / total);
  return 0;
}
