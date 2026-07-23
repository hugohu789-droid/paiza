// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid

#include "core/cloud_candidate_source.h"

namespace paiza {

bool CloudCandidateSource::IsAvailable() const {
  // Disabled until a real endpoint is wired up; toggled from settings.
  return enabled_ && !endpoint_url_.empty();
}

std::vector<Candidate> CloudCandidateSource::Query(
    const std::string& latin_input, size_t max_results) {
  (void)latin_input;
  (void)max_results;
  // TODO: WinHTTP POST {latin_input, max_results} -> JSON candidate list.
  // Keep the request budget small (~100 ms) so the UI never stalls.
  return {};
}

}  // namespace paiza
