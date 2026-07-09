// Copyright 2026 Paiza. All rights reserved.
//
// The UTF-8 <-> wide conversion is hand-rolled so core builds without
// Win32; it must behave identically for 16-bit (Windows) and 32-bit
// (macOS/Linux) wchar_t.

#include "common/string_utils.h"

#include <gtest/gtest.h>

namespace paiza {
namespace {

TEST(StringUtilsTest, EmptyStrings) {
  EXPECT_EQ(Utf8ToWide(""), L"");
  EXPECT_EQ(WideToUtf8(L""), "");
}

TEST(StringUtilsTest, AsciiRoundTrip) {
  EXPECT_EQ(Utf8ToWide("hello"), L"hello");
  EXPECT_EQ(WideToUtf8(L"hello"), "hello");
}

TEST(StringUtilsTest, MongolianRoundTrip) {
  // U+1820 U+1828 (Mongolian A, NA): 3-byte UTF-8 sequences.
  const std::string utf8 = "\xE1\xA0\xA0\xE1\xA0\xA8";
  const std::wstring wide = L"\x1820\x1828";
  EXPECT_EQ(Utf8ToWide(utf8), wide);
  EXPECT_EQ(WideToUtf8(wide), utf8);
}

TEST(StringUtilsTest, ChineseRoundTrip) {
  const std::string utf8 = "\xE4\xB8\xAD\xE6\x96\x87";  // 中文
  EXPECT_EQ(WideToUtf8(Utf8ToWide(utf8)), utf8);
}

TEST(StringUtilsTest, SupplementaryPlaneRoundTrip) {
  // U+1D11E MUSICAL SYMBOL G CLEF: 4-byte UTF-8; surrogate pair when
  // wchar_t is 16-bit, single unit when 32-bit.
  const std::string utf8 = "\xF0\x9D\x84\x9E";
  const std::wstring wide = Utf8ToWide(utf8);
  if constexpr (sizeof(wchar_t) == 2) {
    ASSERT_EQ(wide.size(), 2u);
    EXPECT_EQ(wide[0], 0xD834);
    EXPECT_EQ(wide[1], 0xDD1E);
  } else {
    ASSERT_EQ(wide.size(), 1u);
    EXPECT_EQ(static_cast<uint32_t>(wide[0]), 0x1D11Eu);
  }
  EXPECT_EQ(WideToUtf8(wide), utf8);
}

TEST(StringUtilsTest, InvalidUtf8BecomesReplacement) {
  // Lone continuation byte and truncated sequence.
  EXPECT_EQ(Utf8ToWide("\x80"), L"\xFFFD");
  EXPECT_EQ(Utf8ToWide("a\xE1\xA0"), L"a\xFFFD");
  // Overlong encoding of '/' (0xC0 0xAF) must not decode to '/'.
  const std::wstring overlong = Utf8ToWide("\xC0\xAF");
  EXPECT_EQ(overlong.find(L'/'), std::wstring::npos);
}

}  // namespace
}  // namespace paiza
