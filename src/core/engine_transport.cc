// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid

#include "core/engine_transport.h"

namespace paiza {

LineStatus TakeLine(std::string* buffer, std::string* line) {
  const size_t lf = buffer->find('\n');
  if (lf == std::string::npos) {
    // Nothing to deliver. Past the cap, more reading cannot help: no engine
    // may send a line this long, so waiting would only grow the buffer.
    return buffer->size() >= kEngineMaxResponseBytes ? LineStatus::kOverflow
                                                     : LineStatus::kIncomplete;
  }
  if (lf > kEngineMaxResponseBytes) return LineStatus::kOverflow;
  size_t end = lf;
  if (end > 0 && (*buffer)[end - 1] == '\r') --end;  // tolerate CRLF
  line->assign(*buffer, 0, end);
  buffer->erase(0, lf + 1);
  return LineStatus::kComplete;
}

}  // namespace paiza
