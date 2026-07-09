// Copyright 2026 Paiza. All rights reserved.
//
// High-level facade over NamedPipeClient for the TSF module: typed queries,
// plus best-effort auto-start of the service executable when the pipe is
// not available (fails silently inside sandboxed hosts that cannot spawn
// processes; callers fall back to the built-in transliterator).

#ifndef PAIZA_IPC_IME_CLIENT_H_
#define PAIZA_IPC_IME_CLIENT_H_

#include <string>
#include <vector>

#include "core/candidate.h"
#include "ipc/named_pipe_client.h"

namespace paiza::ipc {

class ImeClient {
 public:
  ImeClient() = default;

  // Queries the service for candidates. Returns false when the service is
  // unreachable or the reply is malformed; `candidates` is left empty then.
  bool QueryCandidates(const std::string& latin_input, uint32_t max_results,
                       std::vector<Candidate>* candidates);

  // Asks the service process to show the settings dialog.
  bool ShowSettings();

  // Returns true if the service answers a ping.
  bool IsServiceRunning();

  // Attempts to launch PaizaService.exe from `directory` (usually the
  // directory containing the TSF DLL). Best-effort; at most once per
  // process (guarded by the caller).
  static void StartServiceProcess(const std::wstring& directory);

 private:
  NamedPipeClient client_;
};

}  // namespace paiza::ipc

#endif  // PAIZA_IPC_IME_CLIENT_H_
