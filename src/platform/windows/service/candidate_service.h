// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid
//
// Wires the IPC server to the candidate engine: decodes requests coming
// from TSF clients, queries the engine, encodes replies. Show-settings
// requests are forwarded to the UI thread via a callback.

#ifndef PAIZA_SERVICE_CANDIDATE_SERVICE_H_
#define PAIZA_SERVICE_CANDIDATE_SERVICE_H_

#include <functional>
#include <memory>

#include "core/candidate_engine.h"
#include "core/static_lexicon.h"
#include "core/profile_switch.h"
#include "core/user_dictionary.h"
#include "ipc/message.h"

namespace paiza::service {

class CandidateService {
 public:
  // `show_settings` is invoked (on an IPC worker thread!) when a client
  // asks for the settings dialog; the callee must marshal to its UI thread.
  // `profiles` (optional) supplies the active user dictionary and static
  // lexicon, which receive kCommitCandidate notifications;
  // it is thread-safe by contract and must outlive this service.
  // `static_lexicon` (optional) has its bigram context updated on commit so
  // the next query favours likely successors; thread-safe, must outlive us.
  CandidateService(std::shared_ptr<CandidateEngine> engine,
                   ProfileSwitch* profiles,
                   std::function<void()> show_settings);

  // NamedPipeServer handler entry point. Thread-safe.
  ipc::Message HandleMessage(const ipc::Message& request);

 private:
  std::shared_ptr<CandidateEngine> engine_;
  // Resolved per notification: the active profile decides which user
  // dictionary a commit is recorded into.
  ProfileSwitch* profiles_;
  std::function<void()> show_settings_;
};

}  // namespace paiza::service

#endif  // PAIZA_SERVICE_CANDIDATE_SERVICE_H_
