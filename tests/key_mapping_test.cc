// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid

#include "core/key_mapping.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <thread>

#include "core/key_mapping_store.h"

namespace paiza {
namespace {

std::wstring TempFilePath(const char* name) {
  const std::string path = ::testing::TempDir() + name;
  return std::wstring(path.begin(), path.end());
}

TEST(KeyMappingTest, DefaultUppercaseSecondSet) {
  const KeyMapping mapping = KeyMapping::CreateDefault();
  EXPECT_EQ(mapping.Lookup("F"), L"\x1839");
  EXPECT_EQ(mapping.Lookup("N"), L"\x1829");
  EXPECT_EQ(mapping.Lookup("K"), L"\x183B");
  EXPECT_EQ(mapping.Lookup("C"), L"\x183C");
  EXPECT_EQ(mapping.Lookup("H"), L"\x183E");
  EXPECT_EQ(mapping.Lookup("R"), L"\x183F");
  EXPECT_EQ(mapping.Lookup("L"), L"\x1840");
  EXPECT_EQ(mapping.Lookup("Z"), L"\x1841");
  EXPECT_EQ(mapping.Lookup("Q"), L"\x1842");
}

TEST(KeyMappingTest, DefaultNiruguAndEeSpecials) {
  const KeyMapping mapping = KeyMapping::CreateDefault();
  EXPECT_EQ(mapping.Lookup("I"), L"\x180A");  // NIRUGU
  EXPECT_EQ(mapping.Lookup("E"), L"\x1827");  // EE
}

TEST(KeyMappingTest, DefaultPunctuationMappings) {
  const KeyMapping mapping = KeyMapping::CreateDefault();
  EXPECT_EQ(mapping.Lookup("*"), L"\x1800");  // BIRGA
  EXPECT_EQ(mapping.Lookup("@"), L"\x180B");  // FVS1
  EXPECT_EQ(mapping.Lookup("#"), L"\x180C");  // FVS2
  EXPECT_EQ(mapping.Lookup("$"), L"\x180D");  // FVS3
  EXPECT_EQ(mapping.Lookup("%"), L"\x180F");  // FVS4
  EXPECT_EQ(mapping.Lookup("-"), L"\x180E");  // MVS
  EXPECT_EQ(mapping.Lookup("_"), L"\x202F");  // NNBSP
  // Composes inline with letters: a + MVS + a.
  EXPECT_EQ(mapping.Convert("a-a"), L"\x1820\x180E\x1820");
}

TEST(KeyMappingTest, HashKeyRoundTripsEscapedInFile) {
  const std::wstring path = TempFilePath("paiza_keymap_hash.txt");
  KeyMapping original = KeyMapping::CreateDefault();
  original.Set("#", L"\x1820");  // Non-default value -> written (as a delta).
  ASSERT_TRUE(original.SaveToFile(path));

  // The '#' key must be escaped, not written as a comment line.
  {
    std::ifstream file(std::filesystem::path(path), std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    EXPECT_NE(content.find("\\#\t1820"), std::string::npos);
  }
  KeyMapping reloaded = KeyMapping::CreateDefault();
  ASSERT_TRUE(reloaded.LoadFromFile(path));
  EXPECT_EQ(reloaded.Lookup("#"), L"\x1820");
}

TEST(KeyMappingTest, SaveWritesOnlyNonDefaultEntries) {
  const std::wstring path = TempFilePath("paiza_keymap_delta.txt");
  KeyMapping original = KeyMapping::CreateDefault();
  original.Set("x", L"\x1841");  // Override one default key.
  ASSERT_TRUE(original.SaveToFile(path));

  std::ifstream file(std::filesystem::path(path), std::ios::binary);
  const std::string content((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
  EXPECT_NE(content.find("x\t1841"), std::string::npos);  // The override.
  EXPECT_EQ(content.find("a\t1820"), std::string::npos);   // Default: omitted.
}

TEST(KeyMappingTest, OtherUppercaseFollowsLowercase) {
  const KeyMapping mapping = KeyMapping::CreateDefault();
  EXPECT_EQ(mapping.Convert("A"), mapping.Convert("a"));
  EXPECT_EQ(mapping.Convert("M"), mapping.Convert("m"));
}

TEST(KeyMappingTest, CaseSensitiveConversion) {
  const KeyMapping mapping = KeyMapping::CreateDefault();
  EXPECT_EQ(mapping.Convert("n"), L"\x1828");
  EXPECT_EQ(mapping.Convert("N"), L"\x1829");
}

TEST(KeyMappingTest, SequenceMatchesGreedily) {
  const KeyMapping mapping = KeyMapping::CreateDefault();
  EXPECT_EQ(mapping.Convert("ng"), L"\x1829");
  EXPECT_EQ(mapping.Convert("nga"), L"\x1829\x1820");
}

TEST(KeyMappingTest, SetOverridesAndRemoves) {
  KeyMapping mapping = KeyMapping::CreateDefault();
  mapping.Set("x", L"\x1841");
  EXPECT_EQ(mapping.Lookup("x"), L"\x1841");
  mapping.Set("sh", L"\x1831");
  EXPECT_EQ(mapping.Convert("sha"), L"\x1831\x1820");
  mapping.Set("sh", L"");
  EXPECT_EQ(mapping.Lookup("sh"), L"");
}

TEST(KeyMappingTest, LoadOverlaysFileEntries) {
  const std::wstring path = TempFilePath("paiza_keymap_load.txt");
  {
    std::ofstream file(std::filesystem::path(path),
                       std::ios::binary | std::ios::trunc);
    file << "# comment line\n"
         << "x\t1830\n"
         << "zz\t182A 182A\n"
         << "bogus\tnothex\n"       // Invalid value: ignored.
         << "toolongkey1\t1820\n";  // Invalid key: ignored.
  }
  KeyMapping mapping = KeyMapping::CreateDefault();
  ASSERT_TRUE(mapping.LoadFromFile(path));
  EXPECT_EQ(mapping.Convert("x"), L"\x1830");
  EXPECT_EQ(mapping.Convert("zz"), L"\x182A\x182A");
  // Untouched defaults survive the overlay.
  EXPECT_EQ(mapping.Convert("a"), L"\x1820");
  EXPECT_EQ(mapping.Lookup("bogus"), L"");
}

TEST(KeyMappingTest, MissingFileReturnsFalse) {
  KeyMapping mapping = KeyMapping::CreateDefault();
  EXPECT_FALSE(mapping.LoadFromFile(L"Z:\\no\\such\\keymap.txt"));
  EXPECT_EQ(mapping.Convert("a"), L"\x1820");  // Unchanged.
}

TEST(KeyMappingTest, SaveLoadRoundTrip) {
  const std::wstring path = TempFilePath("paiza_keymap_roundtrip.txt");
  KeyMapping original = KeyMapping::CreateDefault();
  original.Set("x", L"\x1841\x1842");
  ASSERT_TRUE(original.SaveToFile(path));

  // The file carries only overrides; reload overlays them on the default
  // (exactly what KeyMappingStore does), so unlisted keys follow the default.
  KeyMapping reloaded = KeyMapping::CreateDefault();
  ASSERT_TRUE(reloaded.LoadFromFile(path));
  EXPECT_EQ(reloaded.Lookup("x"), L"\x1841\x1842");  // Override applied.
  EXPECT_EQ(reloaded.Lookup("N"), L"\x1829");        // Default preserved.
  EXPECT_EQ(reloaded.Lookup("a"), L"\x1820");
  EXPECT_EQ(reloaded.Convert("ng"), L"\x1829");
  EXPECT_EQ(original.Entries().size(), reloaded.Entries().size());
}

TEST(KeyMappingTest, ValidateAcceptsWellFormedFile) {
  const std::wstring path = TempFilePath("paiza_keymap_valid.txt");
  {
    std::ofstream file(std::filesystem::path(path),
                       std::ios::binary | std::ios::trunc);
    file << "# comment\n\n   \na\t1820\nng 1829\nN 1829 180E\r\n";
  }
  EXPECT_FALSE(ValidateKeyMapFile(path).has_value());
}

TEST(KeyMappingTest, ValidateMissingFileIsValid) {
  EXPECT_FALSE(
      ValidateKeyMapFile(TempFilePath("paiza_keymap_nonexistent.txt"))
          .has_value());
}

TEST(KeyMappingTest, ValidateReportsFirstBadLine) {
  const std::wstring path = TempFilePath("paiza_keymap_badline.txt");
  {
    std::ofstream file(std::filesystem::path(path),
                       std::ios::binary | std::ios::trunc);
    file << "# ok\na\t1820\nb\tnot-hex\nc\t1834\n";
  }
  const auto error = ValidateKeyMapFile(path);
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(error->line_number, 3u);
  EXPECT_EQ(error->line, "b\tnot-hex");
}

TEST(KeyMappingTest, ValidateRejectsBadKeyAndMissingValue) {
  const std::wstring path = TempFilePath("paiza_keymap_badkey.txt");
  {
    std::ofstream file(std::filesystem::path(path),
                       std::ios::binary | std::ios::trunc);
    file << "toolongkey1 1820\n";
  }
  auto error = ValidateKeyMapFile(path);
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(error->line_number, 1u);

  {
    std::ofstream file(std::filesystem::path(path),
                       std::ios::binary | std::ios::trunc);
    file << "a\n";  // Key without any codepoint.
  }
  error = ValidateKeyMapFile(path);
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(error->line_number, 1u);
}

TEST(KeyMappingStoreTest, RejectsInvalidEditAndRestoresFile) {
  const std::wstring path = TempFilePath("paiza_keymap_restore.txt");
  std::error_code ec;
  std::filesystem::remove(path, ec);

  const std::string good = "a\t1841\n";
  {
    std::ofstream file(std::filesystem::path(path),
                       std::ios::binary | std::ios::trunc);
    file << good;
  }
  KeyMappingStore store(path);
  EXPECT_EQ(store.Get().Convert("a"), L"\x1841");
  EXPECT_TRUE(store.file_status().valid);
  const uint64_t good_revision = store.revision();
  const uint64_t good_status_revision = store.status_revision();

  // A botched edit: bad hex on line 2.
  {
    std::ofstream file(std::filesystem::path(path),
                       std::ios::binary | std::ios::trunc);
    file << "a\t1830\nb\tXYZ\n";
  }
  store.ReloadNow();

  // Mapping unchanged, error reported, file restored to the good bytes.
  EXPECT_EQ(store.Get().Convert("a"), L"\x1841");
  EXPECT_EQ(store.revision(), good_revision);
  EXPECT_FALSE(store.file_status().valid);
  EXPECT_EQ(store.file_status().error_line, 2u);
  EXPECT_EQ(store.file_status().error_text, "b\tXYZ");
  EXPECT_TRUE(store.file_status().restored);
  EXPECT_GT(store.status_revision(), good_status_revision);
  {
    std::ifstream file(std::filesystem::path(path), std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    EXPECT_EQ(content, good);
  }

  // A valid edit afterwards applies normally and clears the error.
  {
    std::ofstream file(std::filesystem::path(path),
                       std::ios::binary | std::ios::trunc);
    file << "a\t1834\n";
  }
  store.ReloadNow();
  EXPECT_EQ(store.Get().Convert("a"), L"\x1834");
  EXPECT_TRUE(store.file_status().valid);

  std::filesystem::remove(path, ec);
}

TEST(KeyMappingStoreTest, InvalidFileAtStartupKeepsDefaultsNoRestore) {
  const std::wstring path = TempFilePath("paiza_keymap_bad_start.txt");
  {
    std::ofstream file(std::filesystem::path(path),
                       std::ios::binary | std::ios::trunc);
    file << "garbage line without hex\n";
  }
  KeyMappingStore store(path);
  EXPECT_EQ(store.Get().Convert("a"), L"\x1820");  // Defaults.
  EXPECT_FALSE(store.file_status().valid);
  // Nothing valid ever seen for this path: no content to restore.
  EXPECT_FALSE(store.file_status().restored);

  std::error_code ec;
  std::filesystem::remove(path, ec);
}

TEST(KeyMappingStoreTest, PicksUpFileChanges) {
  const std::wstring path = TempFilePath("paiza_keymap_store.txt");
  std::error_code ec;
  std::filesystem::remove(path, ec);

  KeyMappingStore store(path);
  const uint64_t initial_revision = store.revision();
  EXPECT_EQ(store.Get().Convert("a"), L"\x1820");  // Defaults.

  {
    std::ofstream file(std::filesystem::path(path),
                       std::ios::binary | std::ios::trunc);
    file << "a\t1841\n";
  }
  // The store throttles checks to 500 ms; wait past the window.
  std::this_thread::sleep_for(std::chrono::milliseconds(600));
  EXPECT_EQ(store.Get().Convert("a"), L"\x1841");
  EXPECT_GT(store.revision(), initial_revision);

  std::filesystem::remove(path, ec);
  std::this_thread::sleep_for(std::chrono::milliseconds(600));
  // Deleted file restores defaults.
  EXPECT_EQ(store.Get().Convert("a"), L"\x1820");
}

}  // namespace
}  // namespace paiza
