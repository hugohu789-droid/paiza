// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid

#include "core/levenshtein_automaton.h"

#include <gtest/gtest.h>

#include <string>

#include "core/equivalence_table.h"

namespace paiza {
namespace {

// Feeds every character of `key` to an automaton built for `query`, returning
// the final distance (or -1 if the automaton went dead / not accepting).
int RunToEnd(const std::string& query, const std::string& key, int k,
             const EquivalenceTable* eq = nullptr) {
  LevenshteinAutomaton automaton(query, k, eq);
  LevenshteinAutomaton::State state = automaton.Start();
  for (char c : key) state = automaton.Step(state, c);
  return automaton.IsAccept(state) ? automaton.Distance(state) : -1;
}

TEST(LevenshteinAutomatonTest, ExactMatchDistanceZero) {
  EXPECT_EQ(RunToEnd("sain", "sain", 1), 0);
}

TEST(LevenshteinAutomatonTest, SingleSubstitution) {
  EXPECT_EQ(RunToEnd("sain", "saik", 1), 1);  // n -> k.
}

TEST(LevenshteinAutomatonTest, SingleInsertionAndDeletion) {
  EXPECT_EQ(RunToEnd("sain", "sainn", 1), 1);  // Extra key: deletion.
  EXPECT_EQ(RunToEnd("sain", "san", 1), 1);    // Missing key: insertion.
}

TEST(LevenshteinAutomatonTest, AdjacentTransposition) {
  EXPECT_EQ(RunToEnd("sain", "sian", 1), 1);  // Swap i and a.
  EXPECT_EQ(RunToEnd("form", "fomr", 1), 1);  // Swap r and m.
}

TEST(LevenshteinAutomatonTest, TwoEditsRejectedAtBudgetOne) {
  EXPECT_EQ(RunToEnd("sain", "soik", 1), -1);  // Two substitutions.
}

TEST(LevenshteinAutomatonTest, DeadStatePrunes) {
  LevenshteinAutomaton automaton("sain", 1, nullptr);
  LevenshteinAutomaton::State state = automaton.Start();
  state = automaton.Step(state, 'x');  // 1 edit used up front.
  state = automaton.Step(state, 'y');  // 2nd mismatch...
  state = automaton.Step(state, 'z');  // ...cannot recover.
  EXPECT_FALSE(automaton.CanMatch(state));
}

TEST(LevenshteinAutomatonTest, EquivalenceMakesSubstitutionFree) {
  EquivalenceTable eq;
  eq.LoadFromString("k n\n");
  EXPECT_EQ(RunToEnd("sain", "saik", 1, &eq), 0);  // k == n, no cost.
}

}  // namespace
}  // namespace paiza
