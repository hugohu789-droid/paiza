// Copyright 2026 Paiza. All rights reserved.
//
// Bounded Damerau-Levenshtein automaton over a fixed query string. Feeding it
// the characters of a candidate key one at a time tracks the edit distance
// between the query and that key prefix; a state is "accepting" when the whole
// key is within `max_edits` of the query, and "dead" when no continuation can
// get back under budget (so a trie/DAWG traversal can prune whole subtrees).
//
// The recurrence is the optimal-string-alignment variant of Damerau-
// Levenshtein: insertion, deletion, substitution, and adjacent transposition.
// For max_edits <= 1 (the IME's fault-tolerance budget) this coincides with
// true Damerau-Levenshtein. Substitutions may be made free by an equivalence
// table (configured "same sound, different key" pairs).
//
// This is the DFA half of the Phase 3 "DFA x FST intersection": StaticLexicon
// walks the DAWG and this automaton in lock-step.

#ifndef PAIZA_CORE_LEVENSHTEIN_AUTOMATON_H_
#define PAIZA_CORE_LEVENSHTEIN_AUTOMATON_H_

#include <string>
#include <vector>

namespace paiza {

class EquivalenceTable;

class LevenshteinAutomaton {
 public:
  // One row of the dynamic-programming table (edit distance of the current
  // key prefix against every prefix of the query). Two consecutive rows are
  // kept so the transposition term d[i-2][j-2] is available.
  struct State {
    std::vector<int> cur;   // d[i][*], i = key-prefix length.
    std::vector<int> prev;  // d[i-1][*]; the Start state seeds this "above"
                            // the budget so transposition is disabled at i<1.
    char last_char = '\0';  // a[i], the last key character consumed.
  };

  // `equivalences` may be null; it must outlive the automaton.
  LevenshteinAutomaton(std::string query, int max_edits,
                       const EquivalenceTable* equivalences);

  State Start() const;
  // Advances by one candidate-key character.
  State Step(const State& state, char key_char) const;

  // Some prefix extension can still finish within budget.
  bool CanMatch(const State& state) const;
  // The key consumed so far is itself within budget of the whole query.
  bool IsAccept(const State& state) const;
  // Edit distance of the consumed key against the query (only meaningful
  // when IsAccept); >= max_edits+1 means "not within budget".
  int Distance(const State& state) const;

  int max_edits() const { return max_edits_; }

 private:
  std::string query_;
  int max_edits_;
  const EquivalenceTable* equivalences_;
};

}  // namespace paiza

#endif  // PAIZA_CORE_LEVENSHTEIN_AUTOMATON_H_
