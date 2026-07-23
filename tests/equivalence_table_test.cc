// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid

#include "core/equivalence_table.h"

#include <gtest/gtest.h>

namespace paiza {
namespace {

TEST(EquivalenceTableTest, EmptyByDefault) {
  EquivalenceTable table;
  EXPECT_TRUE(table.empty());
  EXPECT_TRUE(table.Equivalent('a', 'a'));   // Identity always holds.
  EXPECT_FALSE(table.Equivalent('a', 'b'));
}

TEST(EquivalenceTableTest, GroupsMembersAreEquivalent) {
  EquivalenceTable table;
  const int groups = table.LoadFromString("o q\nu v\n");
  EXPECT_EQ(groups, 2);
  EXPECT_FALSE(table.empty());
  EXPECT_TRUE(table.Equivalent('o', 'q'));
  EXPECT_TRUE(table.Equivalent('q', 'o'));  // Symmetric.
  EXPECT_TRUE(table.Equivalent('u', 'v'));
  EXPECT_FALSE(table.Equivalent('o', 'u'));  // Different groups.
}

TEST(EquivalenceTableTest, TransitiveGrouping) {
  EquivalenceTable table;
  // Three keys on one line, plus a second line folding in a fourth.
  table.LoadFromString("a b c\nc d\n");
  EXPECT_TRUE(table.Equivalent('a', 'd'));  // a-b-c chained with c-d.
  EXPECT_TRUE(table.Equivalent('b', 'd'));
}

TEST(EquivalenceTableTest, CommentsAndLoneKeysIgnored) {
  EquivalenceTable table;
  const int groups = table.LoadFromString("# comment\nx\n\n  y z  \n");
  EXPECT_EQ(groups, 1);  // Only "y z" counts; the lone "x" defines nothing.
  EXPECT_TRUE(table.Equivalent('y', 'z'));
  EXPECT_FALSE(table.Equivalent('x', 'y'));
}

}  // namespace
}  // namespace paiza
