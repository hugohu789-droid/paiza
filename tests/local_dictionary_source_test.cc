// Copyright 2026 Paiza. All rights reserved.

#include "core/local_dictionary_source.h"

#include <gtest/gtest.h>

namespace paiza {
namespace {

class LocalDictionarySourceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    source_.AddEntry("sain", L"\x1830\x1820\x1836\x1822\x1828", 100);
    source_.AddEntry("sain", L"\x1830\x1820\x1822\x1828", 40);
    source_.AddEntry("sar", L"\x1830\x1820\x1837", 60);
    source_.AddEntry("mongol", L"\x182E\x1823\x1829\x182D\x1823\x182F", 90);
  }

  LocalDictionarySource source_;
};

TEST_F(LocalDictionarySourceTest, AvailableOnlyWithEntries) {
  LocalDictionarySource empty;
  EXPECT_FALSE(empty.IsAvailable());
  EXPECT_TRUE(source_.IsAvailable());
}

TEST_F(LocalDictionarySourceTest, ExactMatchBeatsPrefixMatch) {
  const auto results = source_.Query("sain", 9);
  ASSERT_EQ(results.size(), 2u);
  // Exact key, highest frequency first.
  EXPECT_EQ(results[0].text, L"\x1830\x1820\x1836\x1822\x1828");
  EXPECT_EQ(results[0].score, 100);
}

TEST_F(LocalDictionarySourceTest, PrefixSearchFindsAllExtensions) {
  const auto results = source_.Query("sa", 9);
  ASSERT_EQ(results.size(), 3u);  // sain x2 + sar.
}

TEST_F(LocalDictionarySourceTest, CaseSensitiveLookup) {
  // Upper-case letters are distinct keys (Shift second set), so "SAIN"
  // must not match entries stored under "sain".
  EXPECT_TRUE(source_.Query("SAIN", 9).empty());
  source_.AddEntry("saiN", L"\x1830\x1820\x1822\x1829", 10);
  EXPECT_EQ(source_.Query("saiN", 9).size(), 1u);
}

TEST_F(LocalDictionarySourceTest, NoMatchReturnsEmpty) {
  EXPECT_TRUE(source_.Query("zzz", 9).empty());
  EXPECT_TRUE(source_.Query("", 9).empty());
}

TEST_F(LocalDictionarySourceTest, MaxResultsRespected) {
  EXPECT_EQ(source_.Query("sa", 1).size(), 1u);
}

TEST_F(LocalDictionarySourceTest, MissingFileLoadsNothing) {
  LocalDictionarySource missing;
  EXPECT_EQ(missing.LoadFromFile(L"Z:\\does\\not\\exist.txt"), 0u);
  EXPECT_FALSE(missing.IsAvailable());
}

}  // namespace
}  // namespace paiza
