// Copyright 2026 Paiza. All rights reserved.
//
// Loose coupling between the TSF frame and the input algorithm (spec item
// 3): the TSF module never links the dictionary. It asks the service
// process over IPC; when the service is unreachable it degrades to the
// built-in "simple algorithm" (direct transliteration of the key buffer).

#ifndef PAIZA_TSF_ENGINE_PROXY_H_
#define PAIZA_TSF_ENGINE_PROXY_H_

#include <string>
#include <vector>

#include "core/candidate.h"
#include "ipc/ime_client.h"

namespace paiza::tsf {

class EngineProxy {
 public:
  EngineProxy() = default;

  // Returns candidates for the Latin buffer. Never fails: `fallback_text`
  // (the caller's mapped conversion of the buffer) is offered as the sole
  // candidate when the service is unavailable or has no match.
  std::vector<Candidate> QueryCandidates(const std::string& latin_input,
                                         uint32_t max_results,
                                         const std::wstring& fallback_text);

  // Best-effort request to open the settings dialog (service side).
  void RequestShowSettings();

  // Fire-and-forget commit notification: the service updates its dynamic
  // user lexicon (frequency boost / user-coined words). Never blocks
  // typing; a dead service just misses the sample.
  void NotifyCommit(const std::string& latin_input, const std::wstring& word);

 private:
  void MaybeStartService();

  ipc::ImeClient client_;
  bool service_start_attempted_ = false;
};

}  // namespace paiza::tsf

#endif  // PAIZA_TSF_ENGINE_PROXY_H_
