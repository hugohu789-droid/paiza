// Copyright 2026 Paiza. All rights reserved.
//
// Aggregates every registered CandidateSource into a single ranked candidate
// list. Thread-safe for concurrent Query calls once configuration is done.

#ifndef PAIZA_CORE_CANDIDATE_ENGINE_H_
#define PAIZA_CORE_CANDIDATE_ENGINE_H_

#include <memory>
#include <string>
#include <vector>

#include "core/candidate.h"
#include "core/candidate_source.h"

namespace paiza {

class CandidateEngine {
 public:
  CandidateEngine() = default;
  CandidateEngine(const CandidateEngine&) = delete;
  CandidateEngine& operator=(const CandidateEngine&) = delete;

  // Registers a source. Call during startup only (not thread-safe against
  // concurrent Query).
  void AddSource(std::unique_ptr<CandidateSource> source);

  // Queries all available sources in priority order, de-duplicates by text
  // (first occurrence wins) and returns at most `max_results` candidates.
  std::vector<Candidate> Query(const std::string& latin_input,
                               size_t max_results) const;

  size_t source_count() const { return sources_.size(); }

 private:
  std::vector<std::unique_ptr<CandidateSource>> sources_;
};

}  // namespace paiza

#endif  // PAIZA_CORE_CANDIDATE_ENGINE_H_
