// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid

#include "core/romanization.h"

#include <gtest/gtest.h>

#include <string>

#include "core/key_mapping.h"

namespace paiza {
namespace {

TEST(RomanizationTest, InvertsDefaultKeymap) {
  // For the default layout, Romanize(Convert(x)) == x when x uses only
  // single-character keys: the romanized key of what you typed is what you
  // typed. (The one default digraph, "ng"->ANG, normalises to "N" instead;
  // covered separately below.)
  const KeyMapping def = KeyMapping::CreateDefault();
  for (const std::string& latin :
       {"sain", "bagsi", "ulus", "hot", "sar", "ail"}) {
    EXPECT_EQ(Romanize(def.Convert(latin)), latin) << "latin=" << latin;
  }
}

TEST(RomanizationTest, NgDigraphNormalisesToAng) {
  // "ng" is a default keymap digraph mapping to U+1829 (ANG), which romanizes
  // back to the single token "N".
  const KeyMapping def = KeyMapping::CreateDefault();
  EXPECT_EQ(Romanize(def.Convert("mongol")), "moNol");
}

TEST(RomanizationTest, RomanizesMongolianCodepoints) {
  // ᠰᠠᠶᠢᠨ -> s a y i n.
  EXPECT_EQ(Romanize(L"\x1830\x1820\x1836\x1822\x1828"), "sayin");
  // ᠮᠣᠩᠭᠣᠯ uses masculine o (q), ANG (N): m q N g q l.
  EXPECT_EQ(Romanize(L"\x182E\x1823\x1829\x182D\x1823\x182F"), "mqNgql");
}

TEST(RomanizationTest, PassesAsciiThroughAndDropsUnknown) {
  EXPECT_EQ(Romanize(L"ab"), "ab");            // ASCII passes through.
  EXPECT_EQ(Romanize(L"\x1820z\x1821"), "aze");  // Mixed: a + 'z' + e.
}

TEST(RomanizationTest, StripsFormatCharacters) {
  // MVS/FVS/NIRUGU/BIRGA/NNBSP are not part of the lookup key.
  EXPECT_EQ(Romanize(L"\x1830\x180E\x1820"), "sa");  // s + MVS + a
  EXPECT_EQ(Romanize(L"\x1820\x180B\x1821"), "ae");  // a + FVS1 + e
  EXPECT_EQ(Romanize(L"\x1820\x202F\x1821"), "ae");  // a + NNBSP + e
  EXPECT_EQ(Romanize(L"\x1800\x1830"), "s");         // BIRGA + s
}

TEST(RomanizationTest, DetectsMongolianLetters) {
  EXPECT_TRUE(ContainsMongolianLetter(L"\x1820"));            // U+1820.
  EXPECT_TRUE(ContainsMongolianLetter(L"x\x1842y"));          // U+1842.
  EXPECT_FALSE(ContainsMongolianLetter(L"AA"));               // ASCII only.
  EXPECT_FALSE(ContainsMongolianLetter(L"\x1800\x180E"));     // Format only.
}

}  // namespace
}  // namespace paiza
