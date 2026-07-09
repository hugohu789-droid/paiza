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
#include "ipc/message.h"

namespace paiza::service {

class CandidateService {
 public:
  // `show_settings` is invoked (on an IPC worker thread!) when a client
  // asks for the settings dialog; the callee must marshal to its UI thread.
  CandidateService(std::shared_ptr<CandidateEngine> engine,
                   std::function<void()> show_settings);

  // NamedPipeServer handler entry point. Thread-safe.
  ipc::Message HandleMessage(const ipc::Message& request);

 private:
  std::shared_ptr<CandidateEngine> engine_;
  std::function<void()> show_settings_;
};

}  // namespace paiza::service

#endif  // PAIZA_SERVICE_CANDIDATE_SERVICE_H_
