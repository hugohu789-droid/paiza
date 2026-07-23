// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid

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
  // 6 dictionary rows + 1 romanized alias key for "mng" (whose Mongolian
  // U+1820 U+1821 romanizes to "ae", != the gloss key).
  EXPECT_EQ(lex.entry_count(), 7u);
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

// Builds a lexicon from raw dictionary lines plus an embedded equivalences
// blob, returning the .bin path.
std::wstring BuildLexiconWithEquiv(const std::string& dict_lines,
                                   const std::string& equiv_lines,
                                   const char* tag) {
  const std::wstring dict = TempFile((std::string("pz_eq_dict_") + tag).c_str());
  const std::wstring equiv =
      TempFile((std::string("pz_eq_rules_") + tag).c_str());
  const std::wstring bin = TempFile((std::string("pz_eq_bin_") + tag).c_str());
  {
    std::ofstream f(std::filesystem::path(dict),
                    std::ios::binary | std::ios::trunc);
    f << dict_lines;
  }
  {
    std::ofstream f(std::filesystem::path(equiv),
                    std::ios::binary | std::ios::trunc);
    f << equiv_lines;
  }
  LexiconCompiler compiler;
  EXPECT_GE(compiler.AddDictionaryFile(dict), 0);
  EXPECT_TRUE(compiler.SetEquivalenceFile(equiv));
  EXPECT_TRUE(compiler.Write(bin));
  return bin;
}

TEST(StaticLexiconPhraseTest, WholePhraseSurfacesBelowFirstWord) {
  // A phrase entry keyed on the first word: typing "sn" (= sain's key... use
  // ASCII placeholders) surfaces both the single word and the phrase, with the
  // phrase ranked below the single word.
  const std::wstring bin = BuildLexiconWithEquiv(
      "sain\tS1\t100\nsain\tS1 S2\t90\n", "", "phrase");
  StaticLexicon lex;
  ASSERT_TRUE(lex.Load(bin));
  const auto results = lex.Query("sain", 9);
  ASSERT_GE(results.size(), 2u);
  EXPECT_EQ(results[0].text, L"S1");        // Single word first.
  EXPECT_EQ(results[1].text, L"S1 S2");     // Phrase (multi-word) below it.
  EXPECT_EQ(results[1].comment, L"phrase");  // Tagged as a phrase.
  EXPECT_GT(results[0].score, results[1].score);
}

TEST(StaticLexiconSkeletonTest, MatchesConsonantSkeleton) {
  // Keys use ASCII vowels a/i (also Mongolian-vowel keys) and consonants.
  const std::wstring bin = BuildLexiconWithEquiv(
      "sain\tS1\t100\nsar\tS2\t80\n", "", "skel");
  StaticLexicon lex;
  ASSERT_TRUE(lex.Load(bin));
  // "sn" is the consonant skeleton of "sain" (skip a, i).
  bool found = false;
  for (const auto& c : lex.Query("sn", 9))
    if (c.text == L"S1") found = true;
  EXPECT_TRUE(found);
}

TEST(StaticLexiconSkeletonTest, MiddleVowelsOptional) {
  const std::wstring bin =
      BuildLexiconWithEquiv("bagsi\tB1\t100\n", "", "skel2");
  StaticLexicon lex;
  ASSERT_TRUE(lex.Load(bin));
  auto has = [&](const char* q) {
    for (const auto& c : lex.Query(q, 9))
      if (c.text == L"B1") return true;
    return false;
  };
  EXPECT_TRUE(has("bgs"));    // Skip both vowels.
  EXPECT_TRUE(has("bags"));   // Keep one vowel.
  EXPECT_TRUE(has("bagsi"));  // Exact.
}

TEST(StaticLexiconSkeletonTest, LeadingVowelCannotBeOmitted) {
  // "aral" starts with the vowel 'a'. Its consonant-only skeleton "rl" omits
  // the leading vowel and two vowels total (beyond edit-distance-1 tolerance),
  // so it must NOT match; "arl" (leading vowel typed) does.
  const std::wstring bin =
      BuildLexiconWithEquiv("aral\tA1\t100\n", "", "skel3");
  StaticLexicon lex;
  ASSERT_TRUE(lex.Load(bin));
  auto has = [&](const char* q) {
    for (const auto& c : lex.Query(q, 9))
      if (c.text == L"A1") return true;
    return false;
  };
  EXPECT_FALSE(has("rl"));   // Leading vowel omitted -> rejected.
  EXPECT_TRUE(has("arl"));   // Leading vowel typed -> matched.
}

TEST(StaticLexiconSkeletonTest, ExactRanksAboveSkeleton) {
  // Two words: "sn" is exact for one, a skeleton match for the other.
  const std::wstring bin = BuildLexiconWithEquiv(
      "sn\tEXACT\t50\nsan\tSKEL\t100\n", "", "skel4");
  StaticLexicon lex;
  ASSERT_TRUE(lex.Load(bin));
  const auto results = lex.Query("sn", 9);
  ASSERT_GE(results.size(), 2u);
  // Even though SKEL has higher frequency, the exact "sn" match ranks first.
  EXPECT_EQ(results[0].text, L"EXACT");
}

TEST(StaticLexiconFixedSeqTest, EmbedsAndExposesFixedSequences) {
  const std::wstring dict = TempFile("pz_fx_dict.txt");
  const std::wstring fixed = TempFile("pz_fx_rules.txt");
  const std::wstring bin = TempFile("pz_fx_bin.bin");
  const std::string rules = "MVS 1820|1821\n1824 1824\n";
  {
    std::ofstream f(std::filesystem::path(dict),
                    std::ios::binary | std::ios::trunc);
    f << "a\t" << WideToUtf8(L"\x1820") << "\t1\n";
  }
  {
    std::ofstream f(std::filesystem::path(fixed),
                    std::ios::binary | std::ios::trunc);
    f << rules;
  }
  LexiconCompiler compiler;
  ASSERT_GE(compiler.AddDictionaryFile(dict), 0);
  ASSERT_TRUE(compiler.SetFixedSequenceFile(fixed));
  ASSERT_TRUE(compiler.Write(bin));
  StaticLexicon lex;
  ASSERT_TRUE(lex.Load(bin));
  EXPECT_EQ(lex.fixed_sequences(), rules);  // Round-trips inside the binary.
}

TEST(StaticLexiconFixedSeqTest, FindsWordWithStrippedMvs) {
  // A word stored with a medial MVS is found by typing without it (the MVS is
  // stripped from the lookup key).
  const std::wstring dict = TempFile("pz_mvs_dict.txt");
  const std::wstring bin = TempFile("pz_mvs_bin.bin");
  {
    std::ofstream f(std::filesystem::path(dict),
                    std::ios::binary | std::ios::trunc);
    // ᠰᠠ + MVS + ᠨ ; romanizes (MVS stripped) to "san".
    f << "gloss\t" << WideToUtf8(L"\x1830\x1820\x180E\x1828") << "\t50\n";
  }
  LexiconCompiler compiler;
  ASSERT_GE(compiler.AddDictionaryFile(dict), 0);
  ASSERT_TRUE(compiler.Write(bin));
  StaticLexicon lex;
  ASSERT_TRUE(lex.Load(bin));
  EXPECT_FALSE(lex.Query("san", 9).empty());  // No MVS typed.
}

TEST(StaticLexiconRomanizedKeyTest, CompilerAddsRomanizedKey) {
  // A dictionary entry keyed on a gloss is also reachable by the romanized
  // key derived from its Mongolian word (layout-independent lookup).
  const std::wstring dict = TempFile("pz_rk_dict.txt");
  const std::wstring bin = TempFile("pz_rk_bin.bin");
  {
    std::ofstream f(std::filesystem::path(dict),
                    std::ios::binary | std::ios::trunc);
    f << "gloss\t" << WideToUtf8(L"\x182E\x1823\x1829\x182D\x1823\x182F")
      << "\t100\n";  // ᠮᠣᠩᠭᠣᠯ -> "mqNgql"
  }
  LexiconCompiler compiler;
  ASSERT_GE(compiler.AddDictionaryFile(dict), 0);
  ASSERT_TRUE(compiler.Write(bin));
  StaticLexicon lex;
  ASSERT_TRUE(lex.Load(bin));
  EXPECT_FALSE(lex.Query("gloss", 9).empty());    // Original gloss key.
  EXPECT_FALSE(lex.Query("mqNgql", 9).empty());   // Romanized key.
}

TEST(StaticLexiconSoftEquivTest, ExactRanksAboveVariant) {
  // "tal" and "dal" differ only by the t/d soft-equivalence pair.
  const std::wstring bin = BuildLexiconWithEquiv(
      "tal\tTT\t100\ndal\tDD\t100\n", "t d\n", "td");
  StaticLexicon lex;
  ASSERT_TRUE(lex.Load(bin));

  // Typing "tal": the exact spelling TT ranks first; DD is recalled as a
  // variant just below it.
  const auto tal = lex.Query("tal", 9);
  ASSERT_GE(tal.size(), 2u);
  EXPECT_EQ(tal[0].text, L"TT");
  EXPECT_EQ(tal[1].text, L"DD");
  EXPECT_GT(tal[0].score, tal[1].score);

  // Symmetric: typing "dal" puts DD first, TT as the variant.
  const auto dal = lex.Query("dal", 9);
  ASSERT_GE(dal.size(), 2u);
  EXPECT_EQ(dal[0].text, L"DD");
  EXPECT_EQ(dal[1].text, L"TT");
}

TEST(StaticLexiconSoftEquivTest, AllFourPairsRecall) {
  // One word per pair; typing the partner key must recall it.
  const std::wstring bin = BuildLexiconWithEquiv(
      "oro\tOO\t50\nqrq\tQQ\t50\nhag\tHH\t50\ntad\tDD2\t50\n",
      "o u\nq v\nh g\nt d\n", "pairs");
  StaticLexicon lex;
  ASSERT_TRUE(lex.Load(bin));

  auto recalls = [&](const char* typed, const wchar_t* want) {
    for (const auto& c : lex.Query(typed, 9))
      if (c.text == want) return true;
    return false;
  };
  EXPECT_TRUE(recalls("uru", L"OO"));   // o<->u
  EXPECT_TRUE(recalls("vrv", L"QQ"));   // q<->v
  EXPECT_TRUE(recalls("gag", L"HH"));   // h<->g
  EXPECT_TRUE(recalls("dad", L"DD2"));  // t<->d
}

TEST(StaticLexiconSoftEquivTest, EquivalencesTravelInsideBinary) {
  // The equivalence rules are embedded; a freshly loaded lexicon honours them
  // without any runtime LoadTuning call.
  const std::wstring bin = BuildLexiconWithEquiv(
      "tal\tTT\t100\n", "t d\n", "embed");
  StaticLexicon lex;
  ASSERT_TRUE(lex.Load(bin));
  bool found = false;
  for (const auto& c : lex.Query("dal", 9))
    if (c.text == L"TT") found = true;
  EXPECT_TRUE(found);
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
