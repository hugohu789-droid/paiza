// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid
//
// Input profile parsing. The shipped data/profiles/unicode.txt must describe
// today's behaviour exactly, so a profile-driven build is a no-op change.

#include "core/input_profile.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "common/string_utils.h"

namespace paiza {
namespace {

// Writes `content` to `dir/name` and returns the path.
void WriteFile(const std::filesystem::path& path, const std::string& content) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << content;
}

std::filesystem::path MakeTempDir(const char* tag) {
  const std::filesystem::path dir =
      std::filesystem::temp_directory_path() /
      (std::string("paiza_profile_test_") + tag);
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir, ec);
  return dir;
}

TEST(InputProfileTest, ParsesAllKeys) {
  InputProfileRegistry registry;
  ASSERT_TRUE(registry.LoadFromString(
      "id               zcode\n"
      "display_name     ZCode (shape codes)\n"
      "keymap           keymap-zcode.txt\n"
      "lexicon          lexicon-zcode.bin\n"
      "user_db          user-zcode.db\n"
      "font             Menksoft Qagan\n"
      "fixed_sequences  no\n"
      "romanize_query   no\n"
      "skeleton_input   no\n"
      "vowel_keys       aeiou\n"
      "inline_default   hidden\n"));

  const InputProfile* profile = registry.Find("zcode");
  ASSERT_NE(profile, nullptr);
  EXPECT_EQ(profile->id, "zcode");
  EXPECT_EQ(profile->display_name, L"ZCode (shape codes)");
  EXPECT_EQ(profile->keymap, L"keymap-zcode.txt");
  EXPECT_EQ(profile->lexicon, L"lexicon-zcode.bin");
  EXPECT_EQ(profile->user_db, L"user-zcode.db");
  EXPECT_FALSE(profile->fixed_sequences);
  EXPECT_FALSE(profile->romanize_query);
  EXPECT_FALSE(profile->skeleton_input);
  EXPECT_EQ(profile->vowel_keys, "aeiou");
  EXPECT_EQ(profile->inline_default, InlineComposition::kHidden);
}

// A font family keeps its spaces: the value is the rest of the line, not just
// the next token.
TEST(InputProfileTest, FontFamilyKeepsSpaces) {
  InputProfileRegistry registry;
  ASSERT_TRUE(registry.LoadFromString("id x\nfont Noto Sans Mongolian\n"));
  EXPECT_EQ(registry.Find("x")->font, L"Noto Sans Mongolian");
}

// Omitted keys must keep the Unicode behaviour, so an older build loading a
// sparse profile does not silently change how the engine behaves.
TEST(InputProfileTest, OmittedKeysDefaultToUnicodeBehaviour) {
  InputProfileRegistry registry;
  ASSERT_TRUE(registry.LoadFromString("id minimal\n"));
  const InputProfile* profile = registry.Find("minimal");
  ASSERT_NE(profile, nullptr);
  EXPECT_TRUE(profile->fixed_sequences);
  EXPECT_TRUE(profile->romanize_query);
  EXPECT_TRUE(profile->skeleton_input);
  EXPECT_EQ(profile->inline_default, InlineComposition::kFull);
  // display_name falls back to the id so the dropdown is never blank.
  EXPECT_EQ(profile->display_name, L"minimal");
}

TEST(InputProfileTest, RejectsProfileWithoutId) {
  InputProfileRegistry registry;
  EXPECT_FALSE(registry.LoadFromString("display_name No Id\nfont Something\n"));
  EXPECT_TRUE(registry.empty());
}

TEST(InputProfileTest, CommentsAndBlankLinesIgnored) {
  InputProfileRegistry registry;
  ASSERT_TRUE(registry.LoadFromString(
      "# leading comment\n"
      "\n"
      "   \n"
      "id commented\n"
      "   # indented comment\n"
      "font A Font\n"));
  ASSERT_NE(registry.Find("commented"), nullptr);
  EXPECT_EQ(registry.Find("commented")->font, L"A Font");
}

TEST(InputProfileTest, UnknownKeysIgnored) {
  InputProfileRegistry registry;
  ASSERT_TRUE(
      registry.LoadFromString("id future\nsome_future_key whatever\n"));
  EXPECT_NE(registry.Find("future"), nullptr);
}

// An unrecognised boolean keeps the default rather than silently flipping it.
TEST(InputProfileTest, UnrecognisedBooleanKeepsDefault) {
  InputProfileRegistry registry;
  ASSERT_TRUE(registry.LoadFromString("id b\nfixed_sequences maybe\n"));
  EXPECT_TRUE(registry.Find("b")->fixed_sequences);
}

TEST(InputProfileTest, SameIdReplacesEarlierProfile) {
  InputProfileRegistry registry;
  ASSERT_TRUE(registry.LoadFromString("id dup\nfont First\n"));
  ASSERT_TRUE(registry.LoadFromString("id dup\nfont Second\n"));
  EXPECT_EQ(registry.size(), 1u);
  EXPECT_EQ(registry.Find("dup")->font, L"Second");
}

TEST(InputProfileTest, FindReturnsNullForUnknownId) {
  InputProfileRegistry registry;
  ASSERT_TRUE(registry.LoadFromString("id known\n"));
  EXPECT_EQ(registry.Find("unknown"), nullptr);
}

TEST(InputProfileTest, CrLfFilesParse) {
  InputProfileRegistry registry;
  ASSERT_TRUE(registry.LoadFromString("id crlf\r\nfont Some Font\r\n"));
  EXPECT_EQ(registry.Find("crlf")->font, L"Some Font");
}

TEST(InputProfileTest, LoadsDirectorySortedByFileName) {
  const std::filesystem::path dir = MakeTempDir("dir");
  WriteFile(dir / "b_second.txt", "id second\n");
  WriteFile(dir / "a_first.txt", "id first\n");
  WriteFile(dir / "ignored.conf", "id ignored\n");

  InputProfileRegistry registry;
  EXPECT_EQ(registry.LoadFromDirectory(dir.wstring()), 2);
  ASSERT_EQ(registry.size(), 2u);
  EXPECT_EQ(registry.All()[0].id, "first");  // a_first.txt sorts first.
  EXPECT_EQ(registry.All()[1].id, "second");
  EXPECT_EQ(registry.Find("ignored"), nullptr);  // Not a .txt file.

  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
}

TEST(InputProfileTest, MissingDirectoryIsHarmless) {
  InputProfileRegistry registry;
  EXPECT_EQ(registry.LoadFromDirectory(L"no_such_directory_here"), 0);
  EXPECT_TRUE(registry.empty());
}

TEST(InputProfileTest, InlineCompositionNumericRoundTrips) {
  EXPECT_EQ(InlineCompositionFromValue(0), InlineComposition::kFull);
  EXPECT_EQ(InlineCompositionFromValue(1), InlineComposition::kPlaceholder);
  EXPECT_EQ(InlineCompositionFromValue(2), InlineComposition::kHidden);
  // Out of range must not silently change behaviour.
  EXPECT_EQ(InlineCompositionFromValue(99), InlineComposition::kFull);
}

TEST(InputProfileTest, InlineCompositionRoundTrips) {
  for (InlineComposition value :
       {InlineComposition::kFull, InlineComposition::kPlaceholder,
        InlineComposition::kHidden}) {
    EXPECT_EQ(ParseInlineComposition(InlineCompositionName(value),
                                     InlineComposition::kFull),
              value);
  }
  EXPECT_EQ(ParseInlineComposition("nonsense", InlineComposition::kHidden),
            InlineComposition::kHidden);
}

// The shipped profile must describe the engine's current behaviour, otherwise
// switching the engine to be profile-driven would not be a no-op.
TEST(InputProfileTest, ShippedUnicodeProfileMatchesCurrentBehaviour) {
  const std::filesystem::path path =
      std::filesystem::path(PAIZA_SOURCE_DIR) / "data" / "profiles" /
      "unicode.txt";
  ASSERT_TRUE(std::filesystem::exists(path)) << path.string();

  InputProfileRegistry registry;
  ASSERT_EQ(registry.LoadFromDirectory(path.parent_path().wstring()), 1);

  const InputProfile* profile = registry.Find("unicode");
  ASSERT_NE(profile, nullptr);
  EXPECT_EQ(profile->keymap, L"keymap.txt");
  EXPECT_EQ(profile->lexicon, L"lexicon.bin");
  EXPECT_EQ(profile->user_db, L"user.db");
  EXPECT_EQ(profile->font, L"Noto Sans Mongolian");
  EXPECT_TRUE(profile->fixed_sequences);
  EXPECT_TRUE(profile->romanize_query);
  EXPECT_TRUE(profile->skeleton_input);
  EXPECT_EQ(profile->vowel_keys, "aeioquvE");
  EXPECT_EQ(profile->inline_default, InlineComposition::kFull);
}

// The built-in fallback stands in for a missing data/profiles directory, so
// it must stay identical to the shipped file or the two would silently drift.
TEST(InputProfileTest, BuiltInProfileMatchesShippedFile) {
  const std::filesystem::path dir =
      std::filesystem::path(PAIZA_SOURCE_DIR) / "data" / "profiles";
  InputProfileRegistry registry;
  ASSERT_EQ(registry.LoadFromDirectory(dir.wstring()), 1);
  const InputProfile* shipped = registry.Find(kDefaultProfileId);
  ASSERT_NE(shipped, nullptr);

  const InputProfile built_in = BuiltInUnicodeProfile();
  EXPECT_EQ(built_in.id, shipped->id);
  EXPECT_EQ(built_in.display_name, shipped->display_name);
  EXPECT_EQ(built_in.keymap, shipped->keymap);
  EXPECT_EQ(built_in.lexicon, shipped->lexicon);
  EXPECT_EQ(built_in.user_db, shipped->user_db);
  EXPECT_EQ(built_in.font, shipped->font);
  EXPECT_EQ(built_in.fixed_sequences, shipped->fixed_sequences);
  EXPECT_EQ(built_in.romanize_query, shipped->romanize_query);
  EXPECT_EQ(built_in.skeleton_input, shipped->skeleton_input);
  EXPECT_EQ(built_in.vowel_keys, shipped->vowel_keys);
  EXPECT_EQ(built_in.inline_default, shipped->inline_default);
}

TEST(InputProfileTest, EnsureDefaultSeedsOnlyWhenEmpty) {
  InputProfileRegistry registry;
  registry.EnsureDefault();
  ASSERT_EQ(registry.size(), 1u);
  EXPECT_NE(registry.Find(kDefaultProfileId), nullptr);

  // Already populated: EnsureDefault must not add or replace anything.
  InputProfileRegistry other;
  ASSERT_TRUE(other.LoadFromString("id custom\n"));
  other.EnsureDefault();
  EXPECT_EQ(other.size(), 1u);
  EXPECT_EQ(other.Find(kDefaultProfileId), nullptr);
}

// An engine is declared by data, so a conforming engine can be added without
// touching the host (docs/engine-contract.md).
TEST(InputProfileTest, ParsesEngineDeclaration) {
  InputProfileRegistry registry;
  ASSERT_TRUE(registry.LoadFromString(
      "id eng\n"
      "engine zcode_stdio\n"
      "engine_args --quiet --fast\n"
      "engine_protocol paiza-engine\n"
      "engine_transport stdio\n"
      "engine_priority 90\n"
      "engine_codepoints 1865-18AA\n"));

  const InputProfile* p = registry.Find("eng");
  ASSERT_NE(p, nullptr);
  EXPECT_TRUE(p->has_engine());
  EXPECT_EQ(p->engine, L"zcode_stdio");
  EXPECT_EQ(p->engine_args, L"--quiet --fast");
  EXPECT_EQ(p->engine_protocol, "paiza-engine");
  EXPECT_EQ(p->engine_transport, "stdio");
  EXPECT_EQ(p->engine_priority, 90);
  EXPECT_TRUE(p->engine_codepoints_set);
  EXPECT_EQ(p->engine_codepoint_low, 0x1865u);
  EXPECT_EQ(p->engine_codepoint_high, 0x18AAu);
}

// Every shipped profile so far has no engine; that must stay the default.
TEST(InputProfileTest, ProfileWithoutEngineDeclaresNone) {
  InputProfileRegistry registry;
  ASSERT_TRUE(registry.LoadFromString("id plain\n"));
  const InputProfile* p = registry.Find("plain");
  ASSERT_NE(p, nullptr);
  EXPECT_FALSE(p->has_engine());
  EXPECT_TRUE(p->engine_protocol.empty());
  EXPECT_FALSE(p->engine_codepoints_set);
  EXPECT_EQ(p->engine_priority, 0);
}

// A malformed range must leave validation off rather than on and wrong, which
// would reject every candidate the engine produced.
TEST(InputProfileTest, MalformedCodepointRangeLeavesValidationOff) {
  for (const char* value : {"nonsense", "1865", "18AA-1865", "-18AA",
                            "1865-", "XYZ-18AA", "1865-ZZZ"}) {
    InputProfileRegistry registry;
    ASSERT_TRUE(registry.LoadFromString(std::string("id r\nengine_codepoints ") +
                                        value + "\n"))
        << value;
    EXPECT_FALSE(registry.Find("r")->engine_codepoints_set) << value;
  }
}

TEST(InputProfileTest, SingleCodepointRangeIsAccepted) {
  InputProfileRegistry registry;
  ASSERT_TRUE(
      registry.LoadFromString("id one\nengine_codepoints 1865-1865\n"));
  const InputProfile* p = registry.Find("one");
  EXPECT_TRUE(p->engine_codepoints_set);
  EXPECT_EQ(p->engine_codepoint_low, p->engine_codepoint_high);
}

}  // namespace
}  // namespace paiza
