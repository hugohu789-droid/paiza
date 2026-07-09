// Copyright 2026 Paiza. All rights reserved.

#include "core/transliterator.h"

#include <gtest/gtest.h>

namespace paiza {
namespace {

TEST(TransliteratorTest, EmptyInput) {
  EXPECT_EQ(Transliterator::Convert(""), L"");
}

TEST(TransliteratorTest, SingleVowels) {
  EXPECT_EQ(Transliterator::Convert("a"), L"\x1820");
  EXPECT_EQ(Transliterator::Convert("e"), L"\x1821");
  EXPECT_EQ(Transliterator::Convert("i"), L"\x1822");
  EXPECT_EQ(Transliterator::Convert("o"), L"\x1823");
  EXPECT_EQ(Transliterator::Convert("u"), L"\x1824");
}

TEST(TransliteratorTest, Word) {
  // "sain" -> SA A YI? No: s-a-i-n letter by letter.
  EXPECT_EQ(Transliterator::Convert("sain"),
            L"\x1830\x1820\x1822\x1828");
}

TEST(TransliteratorTest, DigraphNg) {
  // "ng" collapses greedily to U+1829 (ANG): "mongol" has one g, so it
  // yields m-o-ANG-o-l; ᠮᠣᠩᠭᠣᠯ (with GA) needs "monggol".
  EXPECT_EQ(Transliterator::Convert("ng"), L"\x1829");
  EXPECT_EQ(Transliterator::Convert("mongol"),
            L"\x182E\x1823\x1829\x1823\x182F");
  EXPECT_EQ(Transliterator::Convert("monggol"),
            L"\x182E\x1823\x1829\x182D\x1823\x182F");
}

TEST(TransliteratorTest, UppercaseIsSecondLetterSet) {
  // Upper-case keys are distinct: N maps to ANG (U+1829), not NA.
  EXPECT_EQ(Transliterator::Convert("saiN"), L"\x1830\x1820\x1822\x1829");
  // Letters without a second-set assignment follow their lower-case value.
  EXPECT_EQ(Transliterator::Convert("Sa"), Transliterator::Convert("sa"));
}

TEST(TransliteratorTest, UnmappedCharactersPassThrough) {
  EXPECT_EQ(Transliterator::Convert("a1"), L"\x1820" L"1");
}

TEST(TransliteratorTest, ComposableChars) {
  EXPECT_TRUE(Transliterator::IsComposableChar('a'));
  EXPECT_TRUE(Transliterator::IsComposableChar('z'));
  EXPECT_TRUE(Transliterator::IsComposableChar('A'));
  EXPECT_TRUE(Transliterator::IsComposableChar('Z'));
  EXPECT_FALSE(Transliterator::IsComposableChar('1'));
  EXPECT_FALSE(Transliterator::IsComposableChar(' '));
}

}  // namespace
}  // namespace paiza
