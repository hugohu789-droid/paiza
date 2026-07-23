// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid
//
// Value type describing one conversion candidate.

#ifndef PAIZA_CORE_CANDIDATE_H_
#define PAIZA_CORE_CANDIDATE_H_

#include <cstdint>
#include <string>

namespace paiza {

struct Candidate {
  // The Mongolian text (UTF-16, Unicode Mongolian block) to be committed.
  std::wstring text;
  // Optional annotation shown next to the candidate (e.g. source tag).
  std::wstring comment;
  // Higher score sorts earlier within the same source priority.
  int32_t score = 0;
};

}  // namespace paiza

#endif  // PAIZA_CORE_CANDIDATE_H_
