// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid
//
// Placeholder cloud candidate source demonstrating the extensibility model.
// A real implementation would issue an HTTPS request (WinHTTP) to a
// prediction endpoint. Future sources (AI assistance, speech, handwriting)
// follow exactly the same pattern: implement CandidateSource, register with
// the engine in the service process.

#ifndef PAIZA_CORE_CLOUD_CANDIDATE_SOURCE_H_
#define PAIZA_CORE_CLOUD_CANDIDATE_SOURCE_H_

#include "core/candidate_source.h"

namespace paiza {

class CloudCandidateSource : public CandidateSource {
 public:
  explicit CloudCandidateSource(std::wstring endpoint_url)
      : endpoint_url_(std::move(endpoint_url)) {}

  // CandidateSource:
  std::wstring name() const override { return L"cloud"; }
  int priority() const override { return 50; }  // Below the local dictionary.
  bool IsAvailable() const override;
  std::vector<Candidate> Query(const std::string& latin_input,
                               size_t max_results) override;

  void set_enabled(bool enabled) { enabled_ = enabled; }

 private:
  std::wstring endpoint_url_;
  bool enabled_ = false;
};

}  // namespace paiza

#endif  // PAIZA_CORE_CLOUD_CANDIDATE_SOURCE_H_
