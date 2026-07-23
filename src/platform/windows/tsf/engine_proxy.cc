// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid

#include "tsf/engine_proxy.h"

#include "tsf/module.h"

namespace paiza::tsf {

void EngineProxy::MaybeStartService() {
  if (service_start_attempted_) return;
  service_start_attempted_ = true;
  ipc::ImeClient::StartServiceProcess(ModuleDirectory());
}

std::vector<Candidate> EngineProxy::QueryCandidates(
    const std::string& latin_input, uint32_t max_results,
    const std::wstring& fallback_text) {
  std::vector<Candidate> candidates;
  if (latin_input.empty()) return candidates;

  if (!client_.QueryCandidates(latin_input, max_results, &candidates)) {
    MaybeStartService();
  }
  if (candidates.empty() && !fallback_text.empty()) {
    // Fallback: the mapped conversion is always offered so the user can
    // keep typing with the service down.
    Candidate fallback;
    fallback.text = fallback_text;
    candidates.push_back(std::move(fallback));
  }
  return candidates;
}

void EngineProxy::RequestShowSettings() {
  if (!client_.ShowSettings()) {
    MaybeStartService();
    client_.ShowSettings();
  }
}

void EngineProxy::NotifyCommit(const std::string& latin_input,
                               const std::wstring& word) {
  if (latin_input.empty() || word.empty()) return;
  client_.NotifyCommit(latin_input, word);  // Best effort by design.
}

}  // namespace paiza::tsf
