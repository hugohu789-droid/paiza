// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid
//
// Performance harness for the compiled static lexicon (Phase 3 acceptance).
// Generates a large synthetic dictionary, compiles it to lexicon.bin, memory-
// maps it, and measures query latency percentiles for exact/prefix and
// fault-tolerant lookups. Reports DAWG compression too.
//
// Usage: lexicon_bench [num_keys] [num_queries]   (defaults: 200000 20000)

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/lexicon_compiler.h"
#include "core/static_lexicon.h"

namespace {

// Small deterministic PRNG so runs are reproducible.
struct Lcg {
  uint64_t s;
  uint32_t Next() {
    s = s * 6364136223846793005ULL + 1442695040888963407ULL;
    return static_cast<uint32_t>(s >> 33);
  }
};

std::string RandomKey(Lcg& rng, int min_len, int max_len) {
  const int len = min_len + static_cast<int>(rng.Next() % (max_len - min_len + 1));
  std::string key;
  key.reserve(len);
  for (int i = 0; i < len; ++i)
    key.push_back(static_cast<char>('a' + rng.Next() % 26));
  return key;
}

double Percentile(std::vector<double>& v, double p) {
  if (v.empty()) return 0.0;
  const size_t idx = static_cast<size_t>(p / 100.0 * (v.size() - 1));
  std::nth_element(v.begin(), v.begin() + idx, v.end());
  return v[idx];
}

}  // namespace

int main(int argc, char** argv) {
  const int num_keys = argc > 1 ? std::atoi(argv[1]) : 200000;
  const int num_queries = argc > 2 ? std::atoi(argv[2]) : 20000;

  const std::filesystem::path dir = std::filesystem::temp_directory_path();
  const std::wstring dict_path = (dir / L"paiza_bench_dict.txt").wstring();
  const std::wstring bin_path = (dir / L"paiza_bench.bin").wstring();

  Lcg rng{0x9E3779B97F4A7C15ULL};
  std::vector<std::string> keys;
  keys.reserve(num_keys);
  {
    std::ofstream f(std::filesystem::path(dict_path),
                    std::ios::binary | std::ios::trunc);
    for (int i = 0; i < num_keys; ++i) {
      std::string key = RandomKey(rng, 3, 8);
      keys.push_back(key);
      // Reuse the key text as an opaque "Mongolian" payload; frequency varies.
      f << key << '\t' << key << '\t' << (rng.Next() % 100000) << '\n';
    }
  }

  auto t0 = std::chrono::steady_clock::now();
  {
    paiza::LexiconCompiler compiler;
    if (compiler.AddDictionaryFile(dict_path) < 0) {
      std::fprintf(stderr, "compile: cannot read dict\n");
      return 1;
    }
    if (!compiler.Write(bin_path)) {
      std::fprintf(stderr, "compile: write failed\n");
      return 1;
    }
  }
  auto t1 = std::chrono::steady_clock::now();

  paiza::StaticLexicon lex;
  if (!lex.Load(bin_path)) {
    std::fprintf(stderr, "load: failed\n");
    return 1;
  }
  auto t2 = std::chrono::steady_clock::now();

  const uintmax_t file_size = std::filesystem::file_size(bin_path);
  const double compile_ms =
      std::chrono::duration<double, std::milli>(t1 - t0).count();
  const double load_ms =
      std::chrono::duration<double, std::milli>(t2 - t1).count();

  std::printf("keys=%d unique=%u dawg_nodes=%u file=%.1fKB\n", num_keys,
              lex.key_count(), lex.dawg_node_count(), file_size / 1024.0);
  std::printf("compile=%.1fms  mmap_load=%.3fms  (DAWG=%.1f%% of unique keys)\n",
              compile_ms, load_ms,
              100.0 * lex.dawg_node_count() / std::max(1u, lex.key_count()));

  // Latency: exact/prefix queries (existing keys) and tolerant queries (each
  // with one injected substitution so the tolerant path runs).
  std::vector<double> exact_us, tolerant_us;
  exact_us.reserve(num_queries);
  tolerant_us.reserve(num_queries);
  for (int i = 0; i < num_queries; ++i) {
    const std::string& base = keys[rng.Next() % keys.size()];

    auto a = std::chrono::steady_clock::now();
    volatile size_t n1 = lex.Query(base, 9).size();
    auto b = std::chrono::steady_clock::now();
    (void)n1;
    exact_us.push_back(
        std::chrono::duration<double, std::micro>(b - a).count());

    std::string typo = base;
    typo[rng.Next() % typo.size()] =
        static_cast<char>('a' + rng.Next() % 26);
    auto c = std::chrono::steady_clock::now();
    volatile size_t n2 = lex.Query(typo, 9).size();
    auto d = std::chrono::steady_clock::now();
    (void)n2;
    tolerant_us.push_back(
        std::chrono::duration<double, std::micro>(d - c).count());
  }

  auto report = [](const char* label, std::vector<double> v) {
    std::printf("%-9s P50=%.1fus  P90=%.1fus  P99=%.1fus  max=%.1fus\n", label,
                Percentile(v, 50), Percentile(v, 90), Percentile(v, 99),
                *std::max_element(v.begin(), v.end()));
  };
  report("exact", exact_us);
  report("tolerant", tolerant_us);

  std::error_code ec;
  std::filesystem::remove(dict_path, ec);
  std::filesystem::remove(bin_path, ec);
  return 0;
}
