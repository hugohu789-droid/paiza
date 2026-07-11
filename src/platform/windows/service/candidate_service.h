// Copyright 2026 Paiza. All rights reserved.
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
#include "core/user_dictionary.h"
#include "ipc/message.h"

namespace paiza::service {

class CandidateService {
 public:
  // `show_settings` is invoked (on an IPC worker thread!) when a client
  // asks for the settings dialog; the callee must marshal to its UI thread.
  // `user_dictionary` (optional) receives kCommitCandidate notifications;
  // it is thread-safe by contract and must outlive this service.
  // `static_lexicon` (optional) has its bigram context updated on commit so
  // the next query favours likely successors; thread-safe, must outlive us.
  CandidateService(std::shared_ptr<CandidateEngine> engine,
                   UserDictionary* user_dictionary,
                   StaticLexicon* static_lexicon,
                   std::function<void()> show_settings);

  // NamedPipeServer handler entry point. Thread-safe.
  ipc::Message HandleMessage(const ipc::Message& request);

 private:
  std::shared_ptr<CandidateEngine> engine_;
  UserDictionary* user_dictionary_;
  StaticLexicon* static_lexicon_;
  std::function<void()> show_settings_;
};

}  // namespace paiza::service

#endif  // PAIZA_SERVICE_CANDIDATE_SERVICE_H_
