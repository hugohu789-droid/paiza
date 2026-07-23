// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid

#include "core/localization.h"

#include <gtest/gtest.h>

#include <set>
#include <string>

namespace paiza {
namespace {

const UiLanguage kAllLanguages[] = {
    UiLanguage::kEnglish,
    UiLanguage::kChinese,
    UiLanguage::kCyrillic,
};

TEST(LocalizationTest, AllStringTablesComplete) {
  for (const UiLanguage language : kAllLanguages) {
    const UiStrings& strings = GetStrings(language);
    // Every field must be a non-empty string.
    for (const wchar_t* field :
         {strings.window_title, strings.app_title, strings.subtitle,
          strings.section_general, strings.candidates_per_page,
          strings.cloud_candidates, strings.toggle_on, strings.toggle_off,
          strings.language_label, strings.candidate_font_label,
          strings.user_dict_label, strings.user_dict_import,
          strings.user_dict_import_result, strings.user_dict_import_failed,
          strings.section_soft_equiv, strings.soft_equiv_hint,
          strings.section_key_mapping,
          strings.edit_mapping_file, strings.shift_label,
          strings.digraphs_label, strings.mapping_hint,
          strings.mapping_hint_generic, strings.menu_settings, strings.save,
          strings.close, strings.language_name}) {
      ASSERT_NE(field, nullptr);
      EXPECT_NE(field[0], L'\0');
    }
  }
}

TEST(LocalizationTest, GenericMappingHintHasPathPlaceholder) {
  // The macOS settings window substitutes the real file path for %ls.
  for (const UiLanguage language : kAllLanguages) {
    EXPECT_NE(std::wstring(GetStrings(language).mapping_hint_generic)
                  .find(L"%ls"),
              std::wstring::npos);
  }
}

TEST(LocalizationTest, LanguageNamesAreDistinct) {
  std::set<std::wstring> names;
  for (const UiLanguage language : kAllLanguages) {
    names.insert(GetStrings(language).language_name);
  }
  EXPECT_EQ(names.size(), 3u);
}

TEST(LocalizationTest, TranslationsDiffer) {
  // Spot-check that the tables are actually translated, not copies.
  EXPECT_STRNE(GetStrings(UiLanguage::kEnglish).save,
               GetStrings(UiLanguage::kChinese).save);
  EXPECT_STRNE(GetStrings(UiLanguage::kEnglish).save,
               GetStrings(UiLanguage::kCyrillic).save);
}

TEST(LocalizationTest, ValueMappingClampsToEnglish) {
  EXPECT_EQ(UiLanguageFromValue(0), UiLanguage::kEnglish);
  EXPECT_EQ(UiLanguageFromValue(1), UiLanguage::kChinese);
  EXPECT_EQ(UiLanguageFromValue(2), UiLanguage::kCyrillic);
  EXPECT_EQ(UiLanguageFromValue(3), UiLanguage::kEnglish);
  EXPECT_EQ(UiLanguageFromValue(0xFFFFFFFF), UiLanguage::kEnglish);
}

TEST(LocalizationTest, DetectSystemLanguageReturnsSupportedValue) {
  const UiLanguage detected = DetectSystemLanguage();
  EXPECT_LT(static_cast<uint32_t>(detected), kUiLanguageCount);
}

}  // namespace
}  // namespace paiza
