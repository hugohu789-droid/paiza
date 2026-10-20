// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid
//
// Round-trips Settings through the registry (HKCU\Software\PaizaIME). The
// test writes to the real per-user hive but restores the previous values
// afterwards so a developer's own configuration is preserved.

#include "service/settings_store.h"

#include <windows.h>

#include <gtest/gtest.h>

namespace paiza::service {
namespace {

class SettingsStoreTest : public ::testing::Test {
 protected:
  void SetUp() override { saved_ = SettingsStore::Load(); }
  void TearDown() override { SettingsStore::Save(saved_); }
  Settings saved_;
};

TEST_F(SettingsStoreTest, RoundTripsAllFields) {
  Settings settings;
  settings.candidates_per_page = 5;
  settings.cloud_source_enabled = true;
  settings.ui_language = UiLanguage::kCyrillic;
  settings.candidate_font = L"Mongolian Tumen Pua";
  settings.active_profile = "zcode";
  SettingsStore::Save(settings);

  const Settings loaded = SettingsStore::Load();
  EXPECT_EQ(loaded.candidates_per_page, 5u);
  EXPECT_TRUE(loaded.cloud_source_enabled);
  EXPECT_EQ(loaded.ui_language, UiLanguage::kCyrillic);
  EXPECT_EQ(loaded.candidate_font, L"Mongolian Tumen Pua");
  EXPECT_EQ(loaded.active_profile, "zcode");
}

TEST_F(SettingsStoreTest, DefaultFontIsNotoSansMongolian) {
  // Wipe the font value; Load must fall back to the Noto default.
  HKEY key = nullptr;
  if (::RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\PaizaIME", 0, KEY_WRITE,
                      &key) == ERROR_SUCCESS) {
    ::RegDeleteValueW(key, L"CandidateFont");
    ::RegCloseKey(key);
  }
  EXPECT_EQ(SettingsStore::Load().candidate_font, kDefaultCandidateFont);
  EXPECT_STREQ(kDefaultCandidateFont, L"Noto Sans Mongolian");
}

TEST_F(SettingsStoreTest, PageSizeClampedToValidRange) {
  Settings settings;
  settings.candidates_per_page = 99;
  SettingsStore::Save(settings);
  EXPECT_LE(SettingsStore::Load().candidates_per_page, 9u);
}

}  // namespace
}  // namespace paiza::service
