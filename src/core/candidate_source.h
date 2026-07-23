// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid
//
// Extensibility point for candidate retrieval. Every way of producing
// candidates (local dictionary, cloud service, AI model, speech, handwriting)
// implements this interface and is registered with CandidateEngine. Sources
// are queried in priority order and their results merged.

#ifndef PAIZA_CORE_CANDIDATE_SOURCE_H_
#define PAIZA_CORE_CANDIDATE_SOURCE_H_

#include <string>
#include <vector>

#include "core/candidate.h"

namespace paiza {

class CandidateSource {
 public:
  virtual ~CandidateSource() = default;

  // Human-readable identifier, used for logging and candidate comments.
  virtual std::wstring name() const = 0;

  // Sources with a larger priority value contribute earlier in the merged
  // candidate list. Local dictionary > cloud > fallback, typically.
  virtual int priority() const = 0;

  // Whether the source can currently serve queries (e.g. a cloud source is
  // unavailable while offline). Unavailable sources are skipped.
  virtual bool IsAvailable() const = 0;

  // Returns up to `max_results` candidates for the Latin key sequence typed
  // so far. Must be safe to call from the IPC worker threads.
  virtual std::vector<Candidate> Query(const std::string& latin_input,
                                       size_t max_results) = 0;
};

}  // namespace paiza

#endif  // PAIZA_CORE_CANDIDATE_SOURCE_H_
