// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid
//
// InputSession is the cross-platform key-policy contract: these tests
// pin the exact behaviour every front-end (Windows TSF, macOS IMK,
// Linux IBus) must expose to the user.

#include "core/input_session.h"

#include <gtest/gtest.h>

#include <memory>

#include "core/local_dictionary_source.h"

namespace paiza {
namespace {

class InputSessionTest : public ::testing::Test {
 protected:
  InputSessionTest()
      : mappings_(L"Z:\\nonexistent\\keymap.txt") {  // Built-in defaults.
    auto dictionary = std::make_unique<LocalDictionarySource>();
    dictionary->AddEntry("sain", L"\x1830\x1820\x1836\x1822\x1828", 100);
    dictionary->AddEntry("sain", L"\x1830\x1820\x1822\x1828", 40);
    dictionary->AddEntry("sar", L"\x1830\x1820\x1837", 60);
    engine_.AddSource(std::move(dictionary));
  }

  InputSession MakeSession(InputSession::Config config = {}) {
    return InputSession(&engine_, &mappings_, config);
  }

  void Type(InputSession& session, const char* letters) {
    for (const char* p = letters; *p != '\0'; ++p) {
      session.HandleLetter(*p);
    }
  }

  CandidateEngine engine_;
  KeyMappingStore mappings_;
};

TEST_F(InputSessionTest, LettersComposePreeditAndCandidates) {
  InputSession session = MakeSession();
  EXPECT_FALSE(session.composing());

  Type(session, "sain");
  EXPECT_TRUE(session.composing());
  // Default mapping: s a i n -> SA A I NA.
  EXPECT_EQ(session.preedit(), L"\x1830\x1820\x1822\x1828");
  // Dictionary candidates, best first.
  ASSERT_EQ(session.candidates().size(), 2u);
  EXPECT_EQ(session.candidates()[0].text, L"\x1830\x1820\x1836\x1822\x1828");
}

TEST_F(InputSessionTest, DigitCommitsCandidateOnCurrentPage) {
  InputSession session = MakeSession();
  Type(session, "sain");
  const auto result = session.HandleDigit(2);
  EXPECT_TRUE(result.handled);
  ASSERT_TRUE(result.commit.has_value());
  EXPECT_EQ(*result.commit, L"\x1830\x1820\x1822\x1828");
  EXPECT_FALSE(session.composing());
}

TEST_F(InputSessionTest, DigitBeyondPagePassesThroughUnchanged) {
  InputSession session = MakeSession();
  Type(session, "sain");  // 2 candidates.
  const auto result = session.HandleDigit(5);
  EXPECT_FALSE(result.handled);
  EXPECT_FALSE(result.commit.has_value());
  EXPECT_TRUE(session.composing());  // Composition untouched.
}

TEST_F(InputSessionTest, SpaceCommitsHighlightedCandidate) {
  InputSession session = MakeSession();
  Type(session, "sain");
  session.MoveSelection(+1);  // Highlight the 2nd candidate.
  const auto result = session.HandleSpace();
  ASSERT_TRUE(result.commit.has_value());
  EXPECT_EQ(*result.commit, L"\x1830\x1820\x1822\x1828");
}

TEST_F(InputSessionTest, EnterCommitsRawConversion) {
  InputSession session = MakeSession();
  Type(session, "sain");
  const auto result = session.HandleEnter();
  ASSERT_TRUE(result.commit.has_value());
  EXPECT_EQ(*result.commit, L"\x1830\x1820\x1822\x1828");  // Not the dict.
}

TEST_F(InputSessionTest, EscapeCancelsWithoutCommit) {
  InputSession session = MakeSession();
  Type(session, "sain");
  const auto result = session.HandleEscape();
  EXPECT_TRUE(result.handled);
  EXPECT_FALSE(result.commit.has_value());
  EXPECT_FALSE(session.composing());
}

TEST_F(InputSessionTest, BackspaceEditsThenCancels) {
  InputSession session = MakeSession();
  Type(session, "sa");
  session.HandleBackspace();
  EXPECT_TRUE(session.composing());
  EXPECT_EQ(session.preedit(), L"\x1830");
  session.HandleBackspace();
  EXPECT_FALSE(session.composing());
}

TEST_F(InputSessionTest, FallbackCandidateWhenDictionaryHasNoMatch) {
  InputSession session = MakeSession();
  Type(session, "zzz");
  ASSERT_EQ(session.candidates().size(), 1u);
  EXPECT_EQ(session.candidates()[0].text, session.preedit());
}

TEST_F(InputSessionTest, UppercaseSelectsSecondLetterSet) {
  InputSession session = MakeSession();
  session.HandleLetter('N');
  EXPECT_EQ(session.preedit(), L"\x1829");  // ANG, not NA.
}

TEST_F(InputSessionTest, MappedPunctuationComposes) {
  InputSession session = MakeSession();
  EXPECT_TRUE(session.IsComposableChar('-'));
  EXPECT_TRUE(session.IsComposableChar('#'));
  EXPECT_TRUE(session.IsComposableChar('_'));
  Type(session, "a");
  EXPECT_TRUE(session.HandleLetter('-').handled);  // MVS
  EXPECT_TRUE(session.HandleLetter('a').handled);
  EXPECT_EQ(session.preedit(), L"\x1820\x180E\x1820");
  // Punctuation can also START a composition (e.g. Birga).
  session.Reset();
  EXPECT_TRUE(session.HandleLetter('*').handled);
  EXPECT_TRUE(session.composing());
  EXPECT_EQ(session.preedit(), L"\x1800");
}

TEST_F(InputSessionTest, DigitsAndUnmappedCharsNeverCompose) {
  InputSession session = MakeSession();
  EXPECT_FALSE(session.IsComposableChar('5'));  // Digits select.
  EXPECT_FALSE(session.IsComposableChar('!'));  // Not in the mapping.
  EXPECT_FALSE(session.IsComposableChar(' '));
  EXPECT_FALSE(session.HandleLetter('!').handled);
  EXPECT_FALSE(session.composing());
}

TEST_F(InputSessionTest, OtherKeyCommitsRawAndPassesThrough) {
  InputSession session = MakeSession();
  Type(session, "sa");
  const std::wstring raw = session.preedit();
  const auto result = session.HandleOtherKey();
  EXPECT_FALSE(result.handled);  // Key continues to the application.
  ASSERT_TRUE(result.commit.has_value());
  EXPECT_EQ(*result.commit, raw);
  EXPECT_FALSE(session.composing());
}

TEST_F(InputSessionTest, KeysWithoutCompositionPassThrough) {
  InputSession session = MakeSession();
  EXPECT_FALSE(session.HandleSpace().handled);
  EXPECT_FALSE(session.HandleEnter().handled);
  EXPECT_FALSE(session.HandleEscape().handled);
  EXPECT_FALSE(session.HandleBackspace().handled);
  EXPECT_FALSE(session.HandleDigit(1).handled);
  EXPECT_FALSE(session.HandleOtherKey().handled);
}

TEST_F(InputSessionTest, PagingAndDigitOnSecondPage) {
  // 12 entries under the same prefix -> 2 pages at page_size 7.
  for (int i = 0; i < 12; ++i) {
    auto extra = std::make_unique<LocalDictionarySource>();
    extra->AddEntry("ba", std::wstring(1, static_cast<wchar_t>(0x1820 + i)),
                    100 - i);
    engine_.AddSource(std::move(extra));
  }
  InputSession session = MakeSession();
  Type(session, "ba");
  ASSERT_EQ(session.candidates().size(), 12u);
  EXPECT_EQ(session.page_count(), 2u);

  EXPECT_TRUE(session.PageForward());
  EXPECT_EQ(session.current_page(), 1u);
  size_t first = 0;
  size_t last = 0;
  session.PageRange(&first, &last);
  EXPECT_EQ(first, 7u);
  EXPECT_EQ(last, 12u);

  // Digit 3 on page 2 commits global index 9.
  const std::wstring expected = session.candidates()[9].text;
  const auto result = session.HandleDigit(3);
  ASSERT_TRUE(result.commit.has_value());
  EXPECT_EQ(*result.commit, expected);

  EXPECT_FALSE(session.PageBackward());  // Reset after commit.
}

TEST_F(InputSessionTest, SelectionWrapsAndPageFollows) {
  for (int i = 0; i < 12; ++i) {
    auto extra = std::make_unique<LocalDictionarySource>();
    extra->AddEntry("ba", std::wstring(1, static_cast<wchar_t>(0x1820 + i)),
                    100 - i);
    engine_.AddSource(std::move(extra));
  }
  InputSession session = MakeSession();
  Type(session, "ba");

  session.MoveSelection(-1);  // Wrap backwards to the last candidate.
  EXPECT_EQ(session.selected_index(), 11u);
  EXPECT_EQ(session.current_page(), 1u);
  session.MoveSelection(+1);  // Wrap forwards to the first.
  EXPECT_EQ(session.selected_index(), 0u);
  EXPECT_EQ(session.current_page(), 0u);
}

TEST_F(InputSessionTest, PageSizeSettingAppliesToLiveSession) {
  for (int i = 0; i < 12; ++i) {
    auto extra = std::make_unique<LocalDictionarySource>();
    extra->AddEntry("ba", std::wstring(1, static_cast<wchar_t>(0x1820 + i)),
                    100 - i);
    engine_.AddSource(std::move(extra));
  }
  InputSession session = MakeSession();
  Type(session, "ba");
  EXPECT_EQ(session.page_count(), 2u);  // 12 candidates / 7 per page.

  session.set_page_size(3);
  EXPECT_EQ(session.page_size(), 3u);
  EXPECT_EQ(session.page_count(), 4u);

  // The page follows the selection when the geometry changes.
  session.MoveSelection(-1);  // Select index 11.
  EXPECT_EQ(session.current_page(), 3u);
  session.set_page_size(9);
  EXPECT_EQ(session.current_page(), 1u);
  EXPECT_EQ(session.selected_index(), 11u);

  session.set_page_size(0);  // Clamped to the valid 1..9 range.
  EXPECT_EQ(session.page_size(), 1u);
  session.set_page_size(50);
  EXPECT_EQ(session.page_size(), 9u);
}

TEST_F(InputSessionTest, ClickCommitsCandidate) {
  InputSession session = MakeSession();
  Type(session, "sain");
  const std::wstring expected = session.candidates()[1].text;
  const auto result = session.CommitCandidate(1);
  ASSERT_TRUE(result.commit.has_value());
  EXPECT_EQ(*result.commit, expected);
  EXPECT_FALSE(session.CommitCandidate(99).handled);
}

}  // namespace
}  // namespace paiza
