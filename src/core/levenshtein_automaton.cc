// Copyright 2026 Paiza. All rights reserved.

#include "core/levenshtein_automaton.h"

#include <algorithm>
#include <utility>

#include "core/equivalence_table.h"

namespace paiza {

LevenshteinAutomaton::LevenshteinAutomaton(std::string query, int max_edits,
                                           const EquivalenceTable* equivalences)
    : query_(std::move(query)),
      max_edits_(max_edits),
      equivalences_(equivalences) {}

LevenshteinAutomaton::State LevenshteinAutomaton::Start() const {
  const size_t n = query_.size();
  State state;
  state.cur.resize(n + 1);
  for (size_t j = 0; j <= n; ++j) state.cur[j] = static_cast<int>(j);
  // d[-1]: seed above the budget so the i<1 transposition term never fires.
  state.prev.assign(n + 1, static_cast<int>(n) + 1);
  state.last_char = '\0';
  return state;
}

LevenshteinAutomaton::State LevenshteinAutomaton::Step(const State& state,
                                                       char key_char) const {
  const size_t n = query_.size();
  State next;
  next.cur.resize(n + 1);
  next.cur[0] = state.cur[0] + 1;  // d[i][0] = i deletions.
  for (size_t j = 1; j <= n; ++j) {
    const char q = query_[j - 1];
    const bool same =
        q == key_char ||
        (equivalences_ != nullptr && equivalences_->Equivalent(q, key_char));
    int best = state.cur[j - 1] + (same ? 0 : 1);          // substitution
    best = std::min(best, state.cur[j] + 1);               // deletion
    best = std::min(best, next.cur[j - 1] + 1);            // insertion
    // Adjacent transposition: key ...a[i-1]a[i] vs query ...b[j-1]b[j] where
    // a[i]==b[j-1] and a[i-1]==b[j]. Uses d[i-2][j-2] = state.prev[j-2].
    if (j >= 2 && key_char == query_[j - 2] &&
        state.last_char == query_[j - 1]) {
      best = std::min(best, state.prev[j - 2] + 1);
    }
    next.cur[j] = best;
  }
  next.prev = state.cur;  // d[i] becomes the new d[i-1].
  next.last_char = key_char;
  return next;
}

bool LevenshteinAutomaton::CanMatch(const State& state) const {
  int lowest = state.cur[0];
  for (int v : state.cur) lowest = std::min(lowest, v);
  return lowest <= max_edits_;
}

bool LevenshteinAutomaton::IsAccept(const State& state) const {
  return state.cur.back() <= max_edits_;
}

int LevenshteinAutomaton::Distance(const State& state) const {
  return state.cur.back();
}

}  // namespace paiza
