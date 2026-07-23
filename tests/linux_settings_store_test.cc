// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid
//
// The Linux INI settings store is pure standard C++, so these tests run
// on every platform — the same cross-platform guard idea as the
// InputSession tests.

#include "settings_store.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>

namespace paiza::linux_ime {
namespace {

class LinuxSettingsStoreTest : public ::testing::Test {
 protected:
  LinuxSettingsStoreTest() {
    path_ = (std::filesystem::temp_directory_path() /
             "paiza_settings_test.ini")
                .wstring();
    std::filesystem::remove(std::filesystem::path(path_));
  }
  ~LinuxSettingsStoreTest() override {
    std::error_code ec;
    std::filesystem::remove(std::filesystem::path(path_), ec);
  }

  std::wstring path_;
};

TEST_F(LinuxSettingsStoreTest, MissingFileYieldsDefaults) {
  const Settings settings = SettingsStore::LoadFromFile(path_);
  EXPECT_EQ(settings.candidates_per_page, 7u);
  EXPECT_FALSE(settings.cloud_source_enabled);
  EXPECT_EQ(settings.candidate_font, kDefaultCandidateFont);
  // Soft-equivalence pairs default to on.
  EXPECT_TRUE(settings.equiv_ou);
  EXPECT_TRUE(settings.equiv_qv);
  EXPECT_TRUE(settings.equiv_hg);
  EXPECT_TRUE(settings.equiv_td);
  EXPECT_TRUE(settings.equiv_ew);
  // ui_language follows the OS locale; it must be a supported value.
  EXPECT_LT(static_cast<uint32_t>(settings.ui_language), kUiLanguageCount);
}

TEST_F(LinuxSettingsStoreTest, RoundTrip) {
  Settings settings;
  settings.candidates_per_page = 4;
  settings.cloud_source_enabled = true;
  settings.ui_language = UiLanguage::kChinese;
  settings.candidate_font = L"Menk Qagan Tig";
  // Flip a couple of soft-equivalence pairs off; the rest stay on.
  settings.equiv_qv = false;
  settings.equiv_ew = false;
  ASSERT_TRUE(SettingsStore::SaveToFile(settings, path_));

  const Settings loaded = SettingsStore::LoadFromFile(path_);
  EXPECT_EQ(loaded.candidates_per_page, 4u);
  EXPECT_TRUE(loaded.cloud_source_enabled);
  EXPECT_EQ(loaded.ui_language, UiLanguage::kChinese);
  EXPECT_EQ(loaded.candidate_font, L"Menk Qagan Tig");
  EXPECT_TRUE(loaded.equiv_ou);
  EXPECT_FALSE(loaded.equiv_qv);
  EXPECT_TRUE(loaded.equiv_hg);
  EXPECT_TRUE(loaded.equiv_td);
  EXPECT_FALSE(loaded.equiv_ew);
}

TEST_F(LinuxSettingsStoreTest, MalformedValuesFallBackAndClamp) {
  std::ofstream file{std::filesystem::path(path_), std::ios::binary};
  file << "# comment\n"
       << "candidates_per_page=99\n"     // Clamped to 9.
       << "cloud_source_enabled=yes\n"   // Not "1"/"true" -> false.
       << "ui_language=57\n"             // Out of range -> English.
       << "candidate_font=\n"            // Empty -> default.
       << "garbage line without equals\n";
  file.close();

  const Settings loaded = SettingsStore::LoadFromFile(path_);
  EXPECT_EQ(loaded.candidates_per_page, 9u);
  EXPECT_FALSE(loaded.cloud_source_enabled);
  EXPECT_EQ(loaded.ui_language, UiLanguage::kEnglish);
  EXPECT_EQ(loaded.candidate_font, kDefaultCandidateFont);
}

TEST_F(LinuxSettingsStoreTest, WatcherPicksUpExternalSaves) {
  Settings settings;
  settings.candidates_per_page = 5;
  ASSERT_TRUE(SettingsStore::SaveToFile(settings, path_));

  SettingsWatcher watcher(path_);
  EXPECT_EQ(watcher.Get().candidates_per_page, 5u);

  // The check is throttled to 500 ms; a fresh watcher sees the change
  // immediately (constructor loads eagerly).
  settings.candidates_per_page = 3;
  ASSERT_TRUE(SettingsStore::SaveToFile(settings, path_));
  SettingsWatcher fresh(path_);
  EXPECT_EQ(fresh.Get().candidates_per_page, 3u);
}

}  // namespace
}  // namespace paiza::linux_ime
