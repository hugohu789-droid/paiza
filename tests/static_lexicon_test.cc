// Copyright 2026 Paiza. All rights reserved.

#include "core/static_lexicon.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "common/string_utils.h"
#include "core/lexicon_compiler.h"

namespace paiza {
namespace {

std::wstring TempFile(const char* name) {
  const std::string p = ::testing::TempDir() + name;
  return std::wstring(p.begin(), p.end());
}

// Builds a small lexicon.bin on disk and returns its path. Entries use ASCII
// "Mongolian" placeholders for readable assertions, plus one real multibyte
// word to prove UTF-8 round-trips through the string pool.
class StaticLexiconTest : public ::testing::Test {
 protected:
  void SetUp() override {
    bin_path_ = TempFile("paiza_lex_test.bin");
    const std::wstring dict = TempFile("paiza_lex_dict.txt");
    const std::wstring bigram = TempFile("paiza_lex_bigram.txt");
    {
      std::ofstream f(std::filesystem::path(dict),
                      std::ios::binary | std::ios::trunc);
      f << "sain\tAA\t100\n"    // id 2 (higher freq sorts first under "sain")
        << "sain\tBB\t40\n"     // id 3
        << "sar\tCC\t50\n"      // id 4
        << "saraa\tDD\t20\n"    // id 5
        << "bagsi\tEE\t70\n"    // id 1 ("bagsi" sorts first)
        << "mng\t" << WideToUtf8(L"\x1820\x1821") << "\t5\n";  // id 6, UTF-8
    }
    {
      std::ofstream f(std::filesystem::path(bigram),
                      std::ios::binary | std::ios::trunc);
      // After "sar/CC", "sain/AA" is a likely successor.
      f << "sar\tCC\tsain\tAA\t9\n";
    }
    LexiconCompiler compiler;
    ASSERT_GE(compiler.AddDictionaryFile(dict), 0);
    ASSERT_GE(compiler.AddBigramFile(bigram), 0);
    ASSERT_TRUE(compiler.Write(bin_path_));
  }

  std::wstring bin_path_;
};

TEST_F(StaticLexiconTest, LoadsAndReportsEntryCount) {
  StaticLexicon lex;
  ASSERT_TRUE(lex.Load(bin_path_));
  EXPECT_EQ(lex.entry_count(), 6u);
  EXPECT_TRUE(lex.IsAvailable());
}

TEST_F(StaticLexiconTest, ExactMatchFrequencyOrder) {
  StaticLexicon lex;
  ASSERT_TRUE(lex.Load(bin_path_));
  const auto results = lex.Query("sain", 9);
  ASSERT_EQ(results.size(), 2u);
  EXPECT_EQ(results[0].text, L"AA");  // freq 100 > 40.
  EXPECT_EQ(results[1].text, L"BB");
}

TEST_F(StaticLexiconTest, PrefixMatchGathersRange) {
  StaticLexicon lex;
  ASSERT_TRUE(lex.Load(bin_path_));
  const auto results = lex.Query("sa", 9);
  // sain(AA,BB), sar(CC), saraa(DD) all share the prefix "sa".
  EXPECT_EQ(results.size(), 4u);
}

TEST_F(StaticLexiconTest, MultibyteRoundTrips) {
  StaticLexicon lex;
  ASSERT_TRUE(lex.Load(bin_path_));
  const auto results = lex.Query("mng", 9);
  ASSERT_EQ(results.size(), 1u);
  EXPECT_EQ(results[0].text, L"\x1820\x1821");
}

TEST_F(StaticLexiconTest, ToleratesSubstitution) {
  StaticLexicon lex;
  ASSERT_TRUE(lex.Load(bin_path_));
  // "saik" is "sain" with the last key mistyped: no exact/prefix hit.
  const auto results = lex.Query("saik", 9);
  bool found_sain = false;
  for (const auto& c : results)
    if (c.text == L"AA" || c.text == L"BB") found_sain = true;
  EXPECT_TRUE(found_sain);
}

TEST_F(StaticLexiconTest, ToleratesTransposition) {
  StaticLexicon lex;
  ASSERT_TRUE(lex.Load(bin_path_));
  // "sian" is "sain" with two keys swapped (Damerau distance 1).
  const auto results = lex.Query("sian", 9);
  bool found_sain = false;
  for (const auto& c : results)
    if (c.text == L"AA") found_sain = true;
  EXPECT_TRUE(found_sain);
}

TEST_F(StaticLexiconTest, WordIdOfResolves) {
  StaticLexicon lex;
  ASSERT_TRUE(lex.Load(bin_path_));
  const uint32_t sar_id = lex.WordIdOf("sar", L"CC");
  EXPECT_NE(sar_id, 0u);
  EXPECT_EQ(lex.WordIdOf("sar", L"nope"), 0u);
  EXPECT_EQ(lex.WordIdOf("zzz", L"CC"), 0u);
}

TEST_F(StaticLexiconTest, BigramContextRewardsSuccessor) {
  StaticLexicon lex;
  ASSERT_TRUE(lex.Load(bin_path_));
  const uint32_t sar_id = lex.WordIdOf("sar", L"CC");
  ASSERT_NE(sar_id, 0u);

  // Without context, "sain/AA" outranks "sain/BB" purely on frequency.
  const auto before = lex.Query("sain", 9);
  ASSERT_EQ(before.size(), 2u);
  const int32_t aa_before = before[0].score;

  lex.SetContextWord(sar_id);
  const auto after = lex.Query("sain", 9);
  ASSERT_EQ(after.size(), 2u);
  // AA is the recorded successor of CC, so its score gains the bigram bonus.
  EXPECT_GT(after[0].score, aa_before);
  EXPECT_EQ(after[0].text, L"AA");
}

TEST_F(StaticLexiconTest, PredictNextReturnsSuccessors) {
  StaticLexicon lex;
  ASSERT_TRUE(lex.Load(bin_path_));
  const uint32_t sar_id = lex.WordIdOf("sar", L"CC");
  const auto predicted = lex.PredictNext(sar_id, 5);
  ASSERT_EQ(predicted.size(), 1u);
  EXPECT_EQ(predicted[0].text, L"AA");
}

TEST_F(StaticLexiconTest, RejectsCorruptedFile) {
  // Flip a byte in the payload; the CRC check must reject the whole file.
  std::string bytes;
  {
    std::ifstream in(std::filesystem::path(bin_path_), std::ios::binary);
    bytes.assign((std::istreambuf_iterator<char>(in)),
                 std::istreambuf_iterator<char>());
  }
  ASSERT_GT(bytes.size(), 120u);
  bytes[bytes.size() - 1] ^= 0xFF;  // Corrupt the last payload byte.
  const std::wstring corrupt = TempFile("paiza_lex_corrupt.bin");
  {
    std::ofstream out(std::filesystem::path(corrupt),
                      std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  }
  StaticLexicon lex;
  EXPECT_FALSE(lex.Load(corrupt));
  EXPECT_FALSE(lex.IsAvailable());
}

TEST_F(StaticLexiconTest, MissingFileStaysUnloaded) {
  StaticLexicon lex;
  EXPECT_FALSE(lex.Load(L"Z:\\no\\such\\lexicon.bin"));
  EXPECT_TRUE(lex.Query("sain", 9).empty());
}

TEST_F(StaticLexiconTest, EquivalenceMakesSubstitutionFree) {
  StaticLexicon lex;
  ASSERT_TRUE(lex.Load(bin_path_));
  // Treat 'k' and 'n' as equivalent keys; "saik" then matches "sain" at
  // zero edit cost, so it ranks like an exact match.
  lex.equivalences().LoadFromString("k n\n");
  const auto results = lex.Query("saik", 9);
  ASSERT_FALSE(results.empty());
  // Highest scorer should be the frequency-100 "AA" spelling.
  EXPECT_EQ(results[0].text, L"AA");
}

}  // namespace
}  // namespace paiza
