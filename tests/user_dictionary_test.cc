// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid

#include "core/user_dictionary.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>

#include "core/candidate_engine.h"
#include "core/input_session.h"
#include "core/key_mapping_store.h"
#include "core/local_dictionary_source.h"

namespace paiza {
namespace {

std::wstring TempPath(const char* name) {
  const std::string path = ::testing::TempDir() + name;
  return std::wstring(path.begin(), path.end());
}

class UserDictionaryTest : public ::testing::Test {
 protected:
  UserDictionaryTest() : db_path_(TempPath("paiza_user_dict_test.db")) {
    std::error_code ec;
    std::filesystem::remove(db_path_, ec);
    std::filesystem::remove(db_path_ + L"-wal", ec);
    std::filesystem::remove(db_path_ + L"-shm", ec);
  }

  const std::wstring db_path_;
};

TEST_F(UserDictionaryTest, RecordCommitCreatesAndBoostsEntries) {
  UserDictionary dict(db_path_);
  EXPECT_FALSE(dict.IsAvailable());

  dict.RecordCommit("sain", L"\x1830\x1820\x1836\x1822\x1828");
  dict.RecordCommit("sain", L"\x1830\x1820\x1836\x1822\x1828");
  dict.RecordCommit("sain", L"\x1830\x1820\x1822\x1828");
  EXPECT_TRUE(dict.IsAvailable());
  EXPECT_EQ(dict.entry_count(), 2u);

  const auto results = dict.Query("sain", 9);
  ASSERT_EQ(results.size(), 2u);
  // Committed twice -> ranks above the once-committed variant.
  EXPECT_EQ(results[0].text, L"\x1830\x1820\x1836\x1822\x1828");
  EXPECT_EQ(results[0].score, 2);
  EXPECT_EQ(results[0].comment, L"user");
}

TEST_F(UserDictionaryTest, PrefixQueryAndExactFirst) {
  UserDictionary dict(db_path_);
  dict.RecordCommit("sa", L"\x1830\x1820");
  dict.RecordCommit("sain", L"\x1830\x1820\x1836\x1822\x1828");
  dict.RecordCommit("sar", L"\x1830\x1820\x1837");

  const auto prefix = dict.Query("sa", 9);
  ASSERT_EQ(prefix.size(), 3u);
  EXPECT_EQ(prefix[0].text, L"\x1830\x1820");  // Exact key first.
  EXPECT_TRUE(dict.Query("zz", 9).empty());
}

TEST_F(UserDictionaryTest, PersistsAcrossInstances) {
  {
    UserDictionary dict(db_path_);
    dict.RecordCommit("bagsi", L"\x182A\x1820\x182D\x1830\x1822");
    dict.RecordCommit("bagsi", L"\x182A\x1820\x182D\x1830\x1822");
    dict.Flush();
  }  // Destructor also flushes; Flush() makes the intent explicit.
  UserDictionary reloaded(db_path_);
  const auto results = reloaded.Query("bagsi", 9);
  ASSERT_EQ(results.size(), 1u);
  EXPECT_EQ(results[0].score, 2);  // Frequency survived the round trip.
}

TEST_F(UserDictionaryTest, ImportSingleWordsAndPhrases) {
  const std::wstring txt = TempPath("paiza_user_import.txt");
  {
    std::ofstream file(std::filesystem::path(txt),
                       std::ios::binary | std::ios::trunc);
    file << "# user dictionary sample\n"
         << "a\t\xE1\xA0\xA0\n"                       // Single letter.
         << "sainuu\t\xE1\xA0\xA0 \xE1\xA0\xA1\t50\n" // Phrase with space.
         << "malformed-line-without-tab\n"            // Skipped.
         << "\tno-key\t3\n";                          // Skipped (empty key).
  }
  UserDictionary dict(db_path_);
  const ImportResult result = dict.ImportFromFile(txt);
  EXPECT_TRUE(result.opened);
  EXPECT_EQ(result.imported, 2);
  EXPECT_EQ(result.skipped, 2);

  const auto phrase = dict.Query("sainuu", 9);
  ASSERT_EQ(phrase.size(), 1u);
  EXPECT_EQ(phrase[0].text, L"\x1820 \x1821");  // Space preserved.
  EXPECT_EQ(phrase[0].score, 50);

  // Importing again keeps the max frequency, doesn't duplicate.
  const ImportResult again = dict.ImportFromFile(txt);
  EXPECT_EQ(again.imported, 2);
  EXPECT_EQ(dict.Query("sainuu", 9).size(), 1u);
  EXPECT_EQ(dict.Query("sainuu", 9)[0].score, 50);
}

TEST_F(UserDictionaryTest, ImportMissingFileReportsNotOpened) {
  UserDictionary dict(db_path_);
  const ImportResult result =
      dict.ImportFromFile(L"Z:\\no\\such\\dictionary.txt");
  EXPECT_FALSE(result.opened);
}

TEST_F(UserDictionaryTest, MemoryOnlyModeWorksWithoutDatabase) {
  UserDictionary dict((std::wstring()));
  dict.RecordCommit("ab", L"\x1820\x182A");
  EXPECT_EQ(dict.Query("ab", 9).size(), 1u);
  dict.Flush();  // No-op, must not hang.
}

TEST_F(UserDictionaryTest, EngineRanksUserWordsAboveLexicon) {
  auto engine = std::make_unique<CandidateEngine>();
  auto lexicon = std::make_unique<LocalDictionarySource>();
  lexicon->AddEntry("sain", L"\x1830\x1820\x1836\x1822\x1828", 100);
  engine->AddSource(std::move(lexicon));

  auto user = std::make_unique<UserDictionary>(db_path_);
  UserDictionary* user_ptr = user.get();
  engine->AddSource(std::move(user));

  user_ptr->RecordCommit("sain", L"\x1830\x1820\x1822\x1828");
  const auto results = engine->Query("sain", 9);
  ASSERT_GE(results.size(), 2u);
  // The user's own word outranks the higher-frequency lexicon entry.
  EXPECT_EQ(results[0].text, L"\x1830\x1820\x1822\x1828");
}

TEST_F(UserDictionaryTest, InputSessionCommitFeedsUserDictionary) {
  CandidateEngine engine;
  auto lexicon = std::make_unique<LocalDictionarySource>();
  lexicon->AddEntry("sain", L"\x1830\x1820\x1836\x1822\x1828", 100);
  engine.AddSource(std::move(lexicon));
  UserDictionary dict(db_path_);
  KeyMappingStore mappings(L"Z:\\nonexistent\\keymap.txt");

  InputSession session(&engine, &mappings);
  session.set_user_dictionary(&dict);
  session.HandleLetter('s');
  session.HandleLetter('a');
  session.HandleLetter('i');
  session.HandleLetter('n');
  const auto result = session.HandleDigit(1);
  ASSERT_TRUE(result.commit.has_value());

  const auto learned = dict.Query("sain", 9);
  ASSERT_EQ(learned.size(), 1u);
  EXPECT_EQ(learned[0].text, *result.commit);
}

}  // namespace
}  // namespace paiza
