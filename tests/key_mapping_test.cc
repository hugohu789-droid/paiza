// Copyright 2026 Paiza. All rights reserved.

#include "core/key_mapping.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
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

  KeyMapping reloaded;  // Empty, no defaults: file must carry everything.
  ASSERT_TRUE(reloaded.LoadFromFile(path));
  EXPECT_EQ(reloaded.Lookup("x"), L"\x1841\x1842");
  EXPECT_EQ(reloaded.Lookup("N"), L"\x1829");
  EXPECT_EQ(reloaded.Lookup("a"), L"\x1820");
  EXPECT_EQ(reloaded.Convert("ng"), L"\x1829");
  EXPECT_EQ(original.Entries().size(), reloaded.Entries().size());
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
