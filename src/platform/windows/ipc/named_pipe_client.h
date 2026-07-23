// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid
//
// Named-pipe client used by the TSF module. One pipe transaction per call
// (connect, send request, read reply, close): stateless and robust against
// service restarts, and connection setup on a local pipe is sub-millisecond.
//
// Why named pipes (design note, spec item 11):
//  * The TSF DLL runs inside arbitrary host processes, including sandboxed
//    browser renderers (low integrity) and UWP AppContainers. Named pipes
//    accept a security descriptor that can explicitly grant those callers.
//  * Message-mode pipes preserve datagram boundaries, which matches our
//    request/reply protocol.
//  * Overlapped I/O gives us hard client-side timeouts so a wedged service
//    can never hang a host application's UI thread.
//  * Alternatives rejected: ALPC (undocumented), out-of-proc COM (heavy,
//    registration burden), raw shared memory (no flow control/eventing).

#ifndef PAIZA_IPC_NAMED_PIPE_CLIENT_H_
#define PAIZA_IPC_NAMED_PIPE_CLIENT_H_

#include <cstdint>
#include <optional>
#include <vector>

#include "ipc/message.h"

namespace paiza::ipc {

inline constexpr wchar_t kPipeName[] = L"\\\\.\\pipe\\PaizaMongolianIme";

class NamedPipeClient {
 public:
  struct Options {
    uint32_t connect_timeout_ms = 200;
    uint32_t io_timeout_ms = 300;
  };

  NamedPipeClient() = default;
  explicit NamedPipeClient(Options options) : options_(options) {}

  // Performs one request/reply transaction. Returns nullopt if the service
  // is unreachable, times out, or replies with a malformed frame.
  std::optional<Message> Transact(const Message& request);

  // Fire-and-forget send (used for kShowSettings). Returns false on failure.
  bool Send(const Message& request);

 private:
  Options options_;
};

}  // namespace paiza::ipc

#endif  // PAIZA_IPC_NAMED_PIPE_CLIENT_H_
