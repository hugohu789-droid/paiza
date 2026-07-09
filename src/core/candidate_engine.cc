// Copyright 2026 Paiza. All rights reserved.

#include "core/candidate_engine.h"

#include <algorithm>
#include <unordered_set>

#include "common/logging.h"

namespace paiza {

void CandidateEngine::AddSource(std::unique_ptr<CandidateSource> source) {
  sources_.push_back(std::move(source));
  // Keep sources ordered by descending priority so Query can simply iterate.
  std::stable_sort(sources_.begin(), sources_.end(),
                   [](const auto& a, const auto& b) {
                     return a->priority() > b->priority();
                   });
}

std::vector<Candidate> CandidateEngine::Query(const std::string& latin_input,
                                              size_t max_results) const {
  std::vector<Candidate> merged;
  if (latin_input.empty() || max_results == 0) return merged;

  std::unordered_set<std::wstring> seen;
  for (const auto& source : sources_) {
    if (merged.size() >= max_results) break;
    if (!source->IsAvailable()) continue;

    std::vector<Candidate> results =
        source->Query(latin_input, max_results - merged.size());
    // Each source returns its own ranking; stabilize by score inside the
    // source's block without reordering across sources.
    std::stable_sort(results.begin(), results.end(),
                     [](const Candidate& a, const Candidate& b) {
                       return a.score > b.score;
                     });
    for (auto& candidate : results) {
      if (merged.size() >= max_results) break;
      if (candidate.text.empty()) continue;
      if (!seen.insert(candidate.text).second) continue;
      merged.push_back(std::move(candidate));
    }
  }
  return merged;
}

}  // namespace paiza
