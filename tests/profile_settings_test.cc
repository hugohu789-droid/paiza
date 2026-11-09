// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid
//
// Per-profile settings packed into one stored string. The format exists so
// that three platform settings stores (registry, text file, NSUserDefaults)
// need no new key per profile, which makes the round trip and the migration
// from the old single value the things worth pinning.

#include "core/profile_settings.h"

#include <gtest/gtest.h>

#include <string>

#include "core/input_profile.h"

namespace paiza {
namespace {

TEST(ProfileSettingsTest, RoundTripsOneProfile) {
  const std::wstring packed =
      SetPerProfileValue(L"", "zcode", L"Z52 Hara Tig");
  EXPECT_EQ(PerProfileValue(packed, "zcode"), L"Z52 Hara Tig");
}

TEST(ProfileSettingsTest, ProfilesDoNotSeeEachOthersValues) {
  std::wstring packed = SetPerProfileValue(L"", "zcode", L"Z52 Hara Tig");
  packed = SetPerProfileValue(packed, "unicode", L"Noto Sans Mongolian");
  EXPECT_EQ(PerProfileValue(packed, "zcode"), L"Z52 Hara Tig");
  EXPECT_EQ(PerProfileValue(packed, "unicode"), L"Noto Sans Mongolian");
  // Which is the whole point: a form-code face chosen for one profile must
  // not become the other profile's font.
  EXPECT_NE(PerProfileValue(packed, "zcode"),
            PerProfileValue(packed, "unicode"));
}

TEST(ProfileSettingsTest, AnUnknownProfileHasNoValue) {
  const std::wstring packed = SetPerProfileValue(L"", "zcode", L"A");
  EXPECT_TRUE(PerProfileValue(packed, "other").empty());
  EXPECT_TRUE(PerProfileValue(L"", "zcode").empty());
}

TEST(ProfileSettingsTest, SettingAProfileTwiceReplacesItsValue) {
  std::wstring packed = SetPerProfileValue(L"", "zcode", L"A");
  packed = SetPerProfileValue(packed, "unicode", L"B");
  packed = SetPerProfileValue(packed, "zcode", L"C");
  EXPECT_EQ(PerProfileValue(packed, "zcode"), L"C");
  EXPECT_EQ(PerProfileValue(packed, "unicode"), L"B")
      << "and leaves the other profile alone";
}

TEST(ProfileSettingsTest, AnEmptyValueRemovesTheEntry) {
  std::wstring packed = SetPerProfileValue(L"", "zcode", L"A");
  packed = SetPerProfileValue(packed, "unicode", L"B");
  packed = SetPerProfileValue(packed, "zcode", L"");
  EXPECT_TRUE(PerProfileValue(packed, "zcode").empty())
      << "so the profile falls back to the font it declares";
  EXPECT_EQ(PerProfileValue(packed, "unicode"), L"B");
}

// What a user already had before the setting was per profile. It answers
// for the default profile only: that is the one it was chosen for, since
// it is the only one that existed then.
//
// Answering for every profile was the first version of this, and it was
// wrong in a way worse than losing a preference -- the ZCode profile
// inherited a Unicode Mongolian font and it overrode the form-code font
// ZCode declares, so the candidates rendered as the Sibe and Manchu
// letters those code points are in Unicode. Wrong font there is wrong
// text.
TEST(ProfileSettingsTest, AnOldSingleValueAnswersForTheDefaultProfileOnly) {
  const std::wstring legacy = L"Noto Sans Mongolian";
  EXPECT_EQ(PerProfileValue(legacy, kDefaultProfileId), legacy);
  EXPECT_TRUE(PerProfileValue(legacy, "zcode").empty())
      << "so the caller falls back to the font that profile declares";
  EXPECT_TRUE(PerProfileValue(legacy, "anything").empty());
}

TEST(ProfileSettingsTest, WritingMigratesAnOldSingleValue) {
  const std::wstring legacy = L"Noto Sans Mongolian";
  // The user picks a face for ZCode; the Unicode profile must keep what it
  // had rather than losing it to the migration.
  const std::wstring packed =
      SetPerProfileValue(legacy, "zcode", L"Z52 Hara Tig");
  EXPECT_EQ(PerProfileValue(packed, "zcode"), L"Z52 Hara Tig");
  EXPECT_EQ(PerProfileValue(packed, kDefaultProfileId), legacy)
      << "the old value belonged to the default profile and is kept there, "
         "rather than being lost to the profile written first";
}

TEST(ProfileSettingsTest, WritingTheSameProfileMigratesWithoutDuplicating) {
  const std::wstring packed =
      SetPerProfileValue(L"Old Face", "unicode", L"New Face");
  EXPECT_EQ(PerProfileValue(packed, "unicode"), L"New Face");
}

// The separator is the one character a value cannot contain, so a value
// carrying it is refused rather than silently truncating the string.
TEST(ProfileSettingsTest, AValueWithTheSeparatorIsRefused) {
  EXPECT_FALSE(IsStorablePerProfileValue(L"Bad;Face"));
  EXPECT_TRUE(IsStorablePerProfileValue(L"Z52 Hara Tig"));
  const std::wstring packed = SetPerProfileValue(L"", "zcode", L"A");
  EXPECT_EQ(SetPerProfileValue(packed, "zcode", L"Bad;Face"), packed)
      << "the stored string is left exactly as it was";
}

TEST(ProfileSettingsTest, FontFamiliesWithSpacesSurvive) {
  const std::wstring packed =
      SetPerProfileValue(L"", "zcode", L"Z52 Hara Tig");
  EXPECT_EQ(PerProfileValue(packed, "zcode"), L"Z52 Hara Tig");
}

TEST(ProfileSettingsTest, AnEmptyProfileIdChangesNothing) {
  const std::wstring packed = SetPerProfileValue(L"", "zcode", L"A");
  EXPECT_EQ(SetPerProfileValue(packed, "", L"B"), packed);
}

}  // namespace
}  // namespace paiza
