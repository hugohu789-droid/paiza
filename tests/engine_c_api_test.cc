// Copyright 2026 Paiza. All rights reserved.

#include "core/engine_c_api.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace {

std::string TempPathUtf8(const char* name) {
  return ::testing::TempDir() + name;
}

class EngineCApiTest : public ::testing::Test {
 protected:
  void SetUp() override {
    db_path_ = TempPathUtf8("paiza_ffi_test.db");
    lexicon_path_ = TempPathUtf8("paiza_ffi_lexicon.txt");
    std::error_code ec;
    std::filesystem::remove(db_path_, ec);
    std::ofstream file(lexicon_path_, std::ios::binary | std::ios::trunc);
    // "sain" with two spellings, UTF-8 encoded Mongolian.
    file << "sain\t\xE1\xA0\xB0\xE1\xA0\xA0\xE1\xA0\xB6\xE1\xA0\xA2"
            "\xE1\xA0\xA8\t100\n"
         << "sain\t\xE1\xA0\xB0\xE1\xA0\xA0\xE1\xA0\xA2\xE1\xA0\xA8\t40\n";
  }
  void TearDown() override { Engine_Destroy(); }

  std::string db_path_;
  std::string lexicon_path_;
};

TEST_F(EngineCApiTest, InitializeSearchCommitRoundTrip) {
  ASSERT_TRUE(Engine_Initialize(db_path_.c_str(), lexicon_path_.c_str(),
                                nullptr));

  const std::string first = Engine_Search("sain", 0);
  EXPECT_NE(first.find("\"candidates\":["), std::string::npos);
  EXPECT_NE(first.find("\"source\":\"lexicon\""), std::string::npos);

  // Committing promotes the word into the user lexicon...
  Engine_CommitCandidate("sain",
                         "\xE1\xA0\xB0\xE1\xA0\xA0\xE1\xA0\xA2\xE1\xA0\xA8");
  const std::string second = Engine_Search("sain", 0);
  // ...which then ranks first, tagged as a user word.
  const size_t user_pos = second.find("\"source\":\"user\"");
  const size_t lexicon_pos = second.find("\"source\":\"lexicon\"");
  ASSERT_NE(user_pos, std::string::npos);
  ASSERT_NE(lexicon_pos, std::string::npos);
  EXPECT_LT(user_pos, lexicon_pos);
}

TEST_F(EngineCApiTest, SearchWithoutInitializeReturnsEmptyJson) {
  Engine_Destroy();
  EXPECT_STREQ(Engine_Search("sain", 0), "{\"candidates\":[]}");
}

TEST_F(EngineCApiTest, ImportUserDictionary) {
  ASSERT_TRUE(Engine_Initialize(db_path_.c_str(), lexicon_path_.c_str(),
                                nullptr));
  const std::string import_path = TempPathUtf8("paiza_ffi_import.txt");
  {
    std::ofstream file(import_path, std::ios::binary | std::ios::trunc);
    file << "hola\t\xE1\xA0\xA0\xE1\xA0\xA1\t7\nbad line\n";
  }
  EXPECT_EQ(Engine_ImportUserDictionary(import_path.c_str()), 1);
  EXPECT_EQ(Engine_ImportUserDictionary("Z:\\missing.txt"), -1);

  const std::string json = Engine_Search("hola", 0);
  EXPECT_NE(json.find("\"source\":\"user\""), std::string::npos);
  EXPECT_NE(json.find("\"freq\":7"), std::string::npos);
}

TEST_F(EngineCApiTest, MissingLexiconReportsFalseButStillWorks) {
  EXPECT_FALSE(Engine_Initialize(db_path_.c_str(), "Z:\\no\\lexicon.txt",
                                 nullptr));
  Engine_CommitCandidate("ab", "\xE1\xA0\xA0");
  const std::string json = Engine_Search("ab", 0);
  EXPECT_NE(json.find("\"source\":\"user\""), std::string::npos);
}

}  // namespace
