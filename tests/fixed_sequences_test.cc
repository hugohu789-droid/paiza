// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid

#include "core/fixed_sequences.h"

#include <gtest/gtest.h>

#include <string>

namespace paiza {
namespace {

// A small table containing the "-dur" case suffix (MVS + d + u|ue + r) and a
// single-codepoint form that must be ignored for insertion.
FixedSequenceTable MakeTable() {
  FixedSequenceTable table;
  table.LoadFromString(
      "# sample\n"
      "MVS 1833 1824|1826 1837\n"  // -dur / -dür
      "MVS 1822\n"                 // single codepoint: ignored
      "1824 1824\n");             // collocation (no MVS): ignored
  return table;
}

TEST(FixedSequencesTest, ParsesOnlyMvsFormsOfLengthTwoPlus) {
  EXPECT_EQ(MakeTable().size(), 1u);  // Only the "-dur" form counts.
}

TEST(FixedSequencesTest, InsertsMvsBeforeSuffix) {
  const FixedSequenceTable table = MakeTable();
  // Stem ᠠᠪ (a, b) + typed suffix d u r -> masculine, MVS inserted, u kept.
  const std::wstring in = L"\x1820\x182A\x1833\x1824\x1837";
  const std::wstring out = table.InsertSuffixMvs(in);
  EXPECT_EQ(out, L"\x1820\x182A\x180E\x1833\x1824\x1837");
}

TEST(FixedSequencesTest, HarmonisesSuffixVowelToFeminineStem) {
  const FixedSequenceTable table = MakeTable();
  // Feminine stem ᠡᠪ (e, b) + typed u -> corrected to ü (U+1826), MVS inserted.
  const std::wstring in = L"\x1821\x182A\x1833\x1824\x1837";
  const std::wstring out = table.InsertSuffixMvs(in);
  EXPECT_EQ(out, L"\x1821\x182A\x180E\x1833\x1826\x1837");
}

TEST(FixedSequencesTest, MatchesEitherHarmonyVariant) {
  const FixedSequenceTable table = MakeTable();
  // Suffix typed with ü on a masculine stem still matches, corrected to u.
  const std::wstring in = L"\x1820\x182A\x1833\x1826\x1837";
  const std::wstring out = table.InsertSuffixMvs(in);
  EXPECT_EQ(out, L"\x1820\x182A\x180E\x1833\x1824\x1837");
}

TEST(FixedSequencesTest, LeavesTextWithoutSuffixUnchanged) {
  const FixedSequenceTable table = MakeTable();
  const std::wstring in = L"\x1820\x182A\x1828";  // a b n, no -dur tail.
  EXPECT_EQ(table.InsertSuffixMvs(in), in);
}

TEST(FixedSequencesTest, DoesNotDoubleInsertWhenMvsPresent) {
  const FixedSequenceTable table = MakeTable();
  // Already has the MVS before the suffix -> unchanged.
  const std::wstring in = L"\x1820\x182A\x180E\x1833\x1824\x1837";
  EXPECT_EQ(table.InsertSuffixMvs(in), in);
}

TEST(FixedSequencesTest, RequiresAStemBeforeTheSuffix) {
  const FixedSequenceTable table = MakeTable();
  // The suffix with no stem before it is not treated as a suffix.
  const std::wstring in = L"\x1833\x1824\x1837";
  EXPECT_EQ(table.InsertSuffixMvs(in), in);
}

TEST(FixedSequencesTest, StandardizesNnbspSuffixToMvs) {
  const FixedSequenceTable table = MakeTable();
  // Stem ᠠᠪ + NNBSP + "dur": the NNBSP becomes MVS, masculine u kept.
  const std::wstring in = L"\x1820\x182A\x202F\x1833\x1824\x1837";
  EXPECT_EQ(table.StandardizeNnbspToMvs(in),
            L"\x1820\x182A\x180E\x1833\x1824\x1837");
  // Feminine stem ᠡᠪ: NNBSP -> MVS and u harmonised to ü.
  const std::wstring fem = L"\x1821\x182A\x202F\x1833\x1824\x1837";
  EXPECT_EQ(table.StandardizeNnbspToMvs(fem),
            L"\x1821\x182A\x180E\x1833\x1826\x1837");
}

TEST(FixedSequencesTest, LeavesNonStandardNnbspEnclitic) {
  const FixedSequenceTable table = MakeTable();
  // ᠠᠪ + NNBSP + "n" (not a table form) is a genuine enclitic: keep the NNBSP.
  const std::wstring in = L"\x1820\x182A\x202F\x1828";
  EXPECT_EQ(table.StandardizeNnbspToMvs(in), in);
}

TEST(FixedSequencesTest, EmptyTableIsNoOp) {
  FixedSequenceTable table;
  EXPECT_TRUE(table.empty());
  const std::wstring in = L"\x1820\x182A\x1833\x1824\x1837";
  EXPECT_EQ(table.InsertSuffixMvs(in), in);
}

}  // namespace
}  // namespace paiza
