// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid
//
// Multi-client named-pipe server hosted by the candidate service process.
// One acceptor thread creates pipe instances; each connected client is
// served on its own worker thread (clients are short-lived: the TSF module
// opens one transaction per query).

#ifndef PAIZA_IPC_NAMED_PIPE_SERVER_H_
#define PAIZA_IPC_NAMED_PIPE_SERVER_H_

#include <windows.h>

#include <atomic>
#include <functional>
#include <thread>
#include <vector>

#include "ipc/message.h"

namespace paiza::ipc {

class NamedPipeServer {
 public:
  // Handler is invoked for every received message; the returned message is
  // written back as the reply. Called on worker threads: must be
  // thread-safe. Return a message with type kAck and empty payload for
  // one-way notifications.
  using Handler = std::function<Message(const Message&)>;

  explicit NamedPipeServer(Handler handler);
  ~NamedPipeServer();
  NamedPipeServer(const NamedPipeServer&) = delete;
  NamedPipeServer& operator=(const NamedPipeServer&) = delete;

  // Starts the acceptor thread. Returns false if the first pipe instance
  // could not be created (typically: another server instance is running).
  bool Start();

  // Stops accepting and joins all threads.
  void Stop();

 private:
  void AcceptLoop();
  void ServeClient(HANDLE pipe);
  HANDLE CreatePipeInstance();

  Handler handler_;
  std::atomic<bool> running_{false};
  std::thread acceptor_;
  HANDLE stop_event_ = nullptr;
  std::vector<std::thread> workers_;
  CRITICAL_SECTION workers_lock_;
};

}  // namespace paiza::ipc

#endif  // PAIZA_IPC_NAMED_PIPE_SERVER_H_
