// Copyright 2026 Paiza. All rights reserved.
//
// Corpus normaliser. Cleans a UTF-8 Mongolian corpus before build_dictionary.
//
// Two things:
//   1. NNBSP->MVS suffix standardisation (always): a token "stem<NNBSP>suffix"
//      whose suffix is a standard MVS-form (GB/T D.2) has its NNBSP replaced by
//      MVS and its vowels harmonised to the stem's gender.
//   2. Homograph handling (same shape, different encoding):
//      * analyse mode (--review / --corrections-skeleton): groups tokens whose
//        equivalence-normalised shape is equal but whose codepoints differ, and
//        writes a human review list + a pre-filled corrections skeleton.
//      * apply mode (--map corrections.tsv): replaces each wrong token with its
//        correct form (+ NNBSP->MVS), producing a standardised corpus.
//
// Usage:
//   analyse: corpus_normalize --review review.txt --corrections-skeleton c.tsv
//                             [--out auto.txt] [--review-min N] corpus.txt ...
//   apply:   corpus_normalize --map c.tsv --out standard.txt corpus.txt ...

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/string_utils.h"
#include "core/fixed_sequences.h"
#include "core/key_mapping.h"
#include "core/romanization.h"

namespace {

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

// "182E 1823" <-> codepoints.
std::string HexOf(const std::wstring& w) {
  std::string out;
  char buf[16];
  for (wchar_t c : w) {
    std::snprintf(buf, sizeof(buf), "%s%04X", out.empty() ? "" : " ",
                  static_cast<unsigned>(c));
    out += buf;
  }
  return out;
}

std::wstring ParseHex(const std::string& s) {
  std::wstring out;
  std::istringstream in(s);
  std::string tok;
  while (in >> tok) {
    const long cp = std::strtol(tok.c_str(), nullptr, 16);
    if (cp > 0) out.push_back(static_cast<wchar_t>(cp));
  }
  return out;
}

// Glyph selectors dropped when comparing shapes (FVS1-4, NIRUGU). MVS/NNBSP
// are structural and kept.
bool IsShapeStrip(wchar_t cp) {
  return cp == 0x180A || cp == 0x180B || cp == 0x180C || cp == 0x180D ||
         cp == 0x180F;
}

// codepoint -> class representative, built from equivalences.txt (latin keys)
// mapped to codepoints through the default keymap.
std::map<wchar_t, wchar_t> BuildShapeReps(const std::string& equiv_text) {
  const paiza::KeyMapping def = paiza::KeyMapping::CreateDefault();
  std::map<wchar_t, wchar_t> reps;
  std::istringstream stream(equiv_text);
  std::string line;
  while (std::getline(stream, line)) {
    std::istringstream tokens(line);
    std::string key;
    std::vector<wchar_t> group;
    while (tokens >> key) {
      if (key[0] == '#') break;
      const std::wstring value = def.Lookup(std::string(1, key[0]));
      if (value.size() == 1) group.push_back(value[0]);
    }
    if (group.size() < 2) continue;
    const wchar_t rep = group.front();
    for (wchar_t cp : group) reps[cp] = rep;
  }
  return reps;
}

std::wstring NormalizeShape(const std::wstring& token,
                            const std::map<wchar_t, wchar_t>& reps) {
  std::wstring out;
  for (wchar_t cp : token) {
    if (IsShapeStrip(cp)) continue;
    auto it = reps.find(cp);
    out.push_back(it == reps.end() ? cp : it->second);
  }
  return out;
}

}  // namespace

int main(int argc, char** argv) {
  std::string out_path;
  std::string fixed_path = "data/fixed_sequences.txt";
  std::string equiv_path = "data/equivalences.txt";
  std::string map_path;
  std::string review_path;
  std::string skeleton_path;
  uint32_t review_min = 3;
  size_t max_contexts = 2;
  std::vector<std::string> inputs;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--out" && i + 1 < argc) out_path = argv[++i];
    else if (arg == "--fixed" && i + 1 < argc) fixed_path = argv[++i];
    else if (arg == "--equiv" && i + 1 < argc) equiv_path = argv[++i];
    else if (arg == "--map" && i + 1 < argc) map_path = argv[++i];
    else if (arg == "--review" && i + 1 < argc) review_path = argv[++i];
    else if (arg == "--corrections-skeleton" && i + 1 < argc)
      skeleton_path = argv[++i];
    else if (arg == "--review-min" && i + 1 < argc)
      review_min = static_cast<uint32_t>(std::atoi(argv[++i]));
    else if (arg.rfind("--", 0) == 0) {
      std::fprintf(stderr, "Unknown flag: %s\n", arg.c_str());
      return 2;
    } else {
      inputs.push_back(arg);
    }
  }
  if (inputs.empty()) {
    std::fprintf(stderr,
                 "Usage: %s [--out f] [--review f --corrections-skeleton f] "
                 "[--map f] input.txt ...\n",
                 argv[0]);
    return 2;
  }

  paiza::FixedSequenceTable fixed;
  fixed.LoadFromString(ReadFile(fixed_path));

  // Apply mode: correct wrong tokens, then NNBSP->MVS.
  if (!map_path.empty()) {
    std::unordered_map<std::wstring, std::wstring> corrections;  // wrong->right
    std::istringstream cf(ReadFile(map_path));
    std::string line;
    while (std::getline(cf, line)) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (line.empty() || line[0] == '#') continue;
      std::vector<std::string> f;
      std::string field;
      std::istringstream ls(line);
      while (std::getline(ls, field, '\t')) f.push_back(field);
      if (f.size() < 2) continue;
      const std::wstring wrong = ParseHex(f[0]);
      const std::wstring right = ParseHex(f.size() >= 4 ? f[2] : f[1]);
      if (!wrong.empty() && !right.empty()) corrections[wrong] = right;
    }
    std::ostringstream out;
    uint64_t corrected = 0;
    for (const std::string& path : inputs) {
      std::ifstream file(path, std::ios::binary);
      if (!file.is_open()) {
        std::fprintf(stderr, "Cannot open %s\n", path.c_str());
        return 1;
      }
      std::string in_line;
      while (std::getline(file, in_line)) {
        if (!in_line.empty() && in_line.back() == '\r') in_line.pop_back();
        std::wstring rebuilt;
        const auto tokens = SplitOnSpaces(paiza::Utf8ToWide(in_line));
        for (size_t t = 0; t < tokens.size(); ++t) {
          std::wstring token = fixed.StandardizeNnbspToMvs(tokens[t]);
          auto it = corrections.find(token);
          if (it != corrections.end()) {
            token = it->second;
            ++corrected;
          }
          if (t > 0) rebuilt.push_back(L' ');
          rebuilt += token;
        }
        out << paiza::WideToUtf8(rebuilt) << '\n';
      }
    }
    std::ofstream f(out_path.empty() ? "standard.txt" : out_path,
                    std::ios::binary | std::ios::trunc);
    f << out.str();
    std::fprintf(stderr, "%llu token corrections applied\n",
                 static_cast<unsigned long long>(corrected));
    return 0;
  }

  // Analyse / step-1 mode.
  const bool analyse = !review_path.empty() || !skeleton_path.empty();
  const std::map<wchar_t, wchar_t> reps =
      analyse ? BuildShapeReps(ReadFile(equiv_path))
              : std::map<wchar_t, wchar_t>{};
  std::unordered_map<std::wstring, uint32_t> word_freq;
  std::unordered_map<std::wstring, std::vector<std::wstring>> word_ctx;

  std::ostringstream out;
  uint64_t nnbsp = 0;
  for (const std::string& path : inputs) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
      std::fprintf(stderr, "Cannot open %s\n", path.c_str());
      return 1;
    }
    std::string in_line;
    while (std::getline(file, in_line)) {
      if (!in_line.empty() && in_line.back() == '\r') in_line.pop_back();
      const std::wstring wline = paiza::Utf8ToWide(in_line);
      std::wstring rebuilt;
      const auto tokens = SplitOnSpaces(wline);
      for (size_t t = 0; t < tokens.size(); ++t) {
        const std::wstring token = fixed.StandardizeNnbspToMvs(tokens[t]);
        if (token != tokens[t]) ++nnbsp;
        if (t > 0) rebuilt.push_back(L' ');
        rebuilt += token;
        if (analyse) {
          word_freq[token]++;
          auto& ctx = word_ctx[token];
          if (ctx.size() < max_contexts) ctx.push_back(wline);
        }
      }
      out << paiza::WideToUtf8(rebuilt) << '\n';
    }
  }
  if (!out_path.empty()) {
    std::ofstream f(out_path, std::ios::binary | std::ios::trunc);
    f << out.str();
  }

  size_t groups = 0;
  if (analyse) {
    // Group tokens by normalised shape.
    std::map<std::wstring, std::vector<std::wstring>> shape_groups;
    for (const auto& [token, freq] : word_freq) {
      (void)freq;
      shape_groups[NormalizeShape(token, reps)].push_back(token);
    }
    std::ostringstream review;
    std::ostringstream skel;
    review << "# Homograph review - encodings that render the same shape.\n"
           << "# Per group SUGGESTED = the highest-frequency variant. Check the\n"
           << "# contexts, then edit corrections.tsv (a skeleton was written):\n"
           << "# keep/retarget, or delete lines for genuinely different words.\n"
           << "# Variant line:  freq  U+codepoints  romanization  [SUGGESTED]\n";
    skel << "# corrections.tsv - machine reads columns 1 & 3 (hex); columns 2 &"
            " 4 are readable Mongolian.\n"
         << "# wrong_hex<TAB>wrong_mong<TAB>right_hex<TAB>right_mong. Delete a "
            "line to skip (different word).\n";
    for (auto& [shape, variants] : shape_groups) {
      if (variants.size() < 2) continue;
      uint32_t total = 0;
      for (const auto& v : variants) total += word_freq[v];
      if (total < review_min) continue;
      std::sort(variants.begin(), variants.end(),
                [&](const std::wstring& a, const std::wstring& b) {
                  return word_freq[a] > word_freq[b];
                });
      const std::wstring& canonical = variants.front();
      ++groups;
      review << "\n# GROUP " << groups << "   normalized = "
             << paiza::Romanize(shape)
             << "   variants = " << variants.size() << "\n";
      for (const auto& v : variants) {
        review << "  " << word_freq[v] << "\tU+"
               << HexOf(v) << "\t" << paiza::Romanize(v)
               << (v == canonical ? "\t[SUGGESTED]" : "") << "\n";
        for (const auto& c : word_ctx[v]) {
          review << "        ctx: " << paiza::WideToUtf8(c) << "\n";
        }
        if (v != canonical) {
          skel << HexOf(v) << "\t" << paiza::WideToUtf8(v) << "\t"
               << HexOf(canonical) << "\t" << paiza::WideToUtf8(canonical)
               << "\n";
        }
      }
    }
    if (!review_path.empty()) {
      std::ofstream f(review_path, std::ios::binary | std::ios::trunc);
      f << review.str();
    }
    if (!skeleton_path.empty()) {
      std::ofstream f(skeleton_path, std::ios::binary | std::ios::trunc);
      f << skel.str();
    }
  }

  std::fprintf(stderr, "%llu NNBSP->MVS conversions, %zu homograph groups\n",
               static_cast<unsigned long long>(nnbsp), groups);
  return 0;
}
