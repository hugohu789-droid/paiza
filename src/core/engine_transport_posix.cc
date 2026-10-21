// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid
//
// SpawnEngineTransport on macOS and Linux: anonymous pipes to the child's
// stdin and stdout, with poll() giving the hard timeout.
//
// Compiled only on those platforms (see CMakeLists.txt), so there is no
// #ifdef here and no platform branch at the call site.

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <string>
#include <vector>

#include "common/string_utils.h"
#include "core/engine_transport.h"

extern char** environ;

namespace paiza {
namespace {

// Splits space-separated engine_args. Quoting is deliberately not supported:
// an argument needing a quote is a sign the profile is doing too much, and a
// quote parser here would be a second place for a profile to surprise us.
std::vector<std::string> SplitArgs(const std::string& args) {
  std::vector<std::string> out;
  size_t i = 0;
  while (i < args.size()) {
    while (i < args.size() && args[i] == ' ') ++i;
    const size_t begin = i;
    while (i < args.size() && args[i] != ' ') ++i;
    if (i > begin) out.push_back(args.substr(begin, i - begin));
  }
  return out;
}

int64_t NowMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(steady_clock::now().time_since_epoch())
      .count();
}

class StdioEngineTransport : public EngineTransport {
 public:
  StdioEngineTransport(pid_t pid, int to_child, int from_child)
      : pid_(pid), to_child_(to_child), from_child_(from_child) {}

  ~StdioEngineTransport() override {
    Close();
    if (pid_ <= 0) return;
    // The caller sends QUIT before destroying us, so the child is usually
    // already on its way out; closing its stdin ends it in any case. Reap it
    // without blocking, then insist, so a wedged engine cannot leave a zombie
    // or an orphan running against the user's dictionary files.
    for (int i = 0; i < 20; ++i) {
      if (waitpid(pid_, nullptr, WNOHANG) != 0) return;
      usleep(5000);
    }
    kill(pid_, SIGKILL);
    waitpid(pid_, nullptr, 0);
  }

  bool alive() const override { return alive_; }

  bool Exchange(std::string_view request, std::string* response) override {
    if (!alive_) return false;
    std::string line(request);
    line.push_back('\n');
    if (!WriteAll(line)) return Die();

    // One deadline for the whole exchange. Computed once, so an engine that
    // dribbles bytes out cannot extend it indefinitely by staying just
    // active enough to reset a per-read timeout.
    const int64_t deadline = NowMs() + kEngineTimeoutMs;
    while (true) {
      switch (TakeLine(&buffer_, response)) {
        case LineStatus::kComplete:
          return true;
        case LineStatus::kOverflow:
          return Die();
        case LineStatus::kIncomplete:
          break;
      }
      const int64_t remaining = deadline - NowMs();
      if (remaining <= 0) return Die();
      struct pollfd pfd = {from_child_, POLLIN, 0};
      const int ready = poll(&pfd, 1, static_cast<int>(remaining));
      if (ready < 0) {
        if (errno == EINTR) continue;  // a signal, not a failure
        return Die();
      }
      if (ready == 0) return Die();  // timed out
      char chunk[4096];
      const ssize_t n = read(from_child_, chunk, sizeof(chunk));
      if (n > 0) {
        buffer_.append(chunk, static_cast<size_t>(n));
        continue;
      }
      if (n == 0) return Die();  // EOF: the engine exited
      if (errno == EINTR || errno == EAGAIN) continue;
      return Die();
    }
  }

 private:
  bool WriteAll(const std::string& data) {
    size_t written = 0;
    while (written < data.size()) {
      const ssize_t n =
          write(to_child_, data.data() + written, data.size() - written);
      if (n > 0) {
        written += static_cast<size_t>(n);
        continue;
      }
      // EPIPE lands here: the engine is gone, which is a failed exchange.
      if (n < 0 && errno == EINTR) continue;
      return false;
    }
    return true;
  }

  // Latches the failure. Everything that can go wrong -- timeout, EOF, an
  // oversized line, a broken pipe -- arrives here, because the caller's
  // response to all of them is the same: stop using this engine.
  bool Die() {
    alive_ = false;
    Close();
    return false;
  }

  void Close() {
    if (to_child_ >= 0) close(to_child_);
    if (from_child_ >= 0) close(from_child_);
    to_child_ = from_child_ = -1;
  }

  pid_t pid_ = -1;
  int to_child_ = -1;
  int from_child_ = -1;
  bool alive_ = true;
  std::string buffer_;
};

}  // namespace

std::unique_ptr<EngineTransport> SpawnEngineTransport(const std::wstring& path,
                                                      const std::wstring& args,
                                                      std::string* reason) {
  const auto fail = [reason](const char* text) {
    if (reason != nullptr) *reason = text;
    return std::unique_ptr<EngineTransport>();
  };
  if (reason != nullptr) reason->clear();
  if (path.empty()) return fail("no engine path");

  int in_pipe[2] = {-1, -1};   // host writes in_pipe[1], child reads [0]
  int out_pipe[2] = {-1, -1};  // child writes out_pipe[1], host reads [0]
  if (pipe(in_pipe) != 0) return fail("could not create a pipe");
  if (pipe(out_pipe) != 0) {
    close(in_pipe[0]);
    close(in_pipe[1]);
    return fail("could not create a pipe");
  }

  const std::string exe = WideToUtf8(path);
  std::vector<std::string> argv_storage = {exe};
  for (std::string& arg : SplitArgs(WideToUtf8(args))) {
    argv_storage.push_back(std::move(arg));
  }
  std::vector<char*> argv;
  for (std::string& arg : argv_storage) argv.push_back(arg.data());
  argv.push_back(nullptr);

  // posix_spawn rather than fork/exec: fork in a process that may hold locks
  // (an IME is loaded into other people's processes) is a known source of
  // deadlocks in the child between fork and exec.
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_adddup2(&actions, in_pipe[0], STDIN_FILENO);
  posix_spawn_file_actions_adddup2(&actions, out_pipe[1], STDOUT_FILENO);
  // The child must not inherit the host's ends, or an EOF would never arrive.
  posix_spawn_file_actions_addclose(&actions, in_pipe[1]);
  posix_spawn_file_actions_addclose(&actions, out_pipe[0]);
  // stderr is left alone: an engine's diagnostics belong in the host's log,
  // and swallowing them makes a misbehaving engine much harder to explain.

  pid_t pid = -1;
  const int rc =
      posix_spawn(&pid, exe.c_str(), &actions, nullptr, argv.data(), environ);
  posix_spawn_file_actions_destroy(&actions);
  close(in_pipe[0]);
  close(out_pipe[1]);
  if (rc != 0) {
    close(in_pipe[1]);
    close(out_pipe[0]);
    return fail(rc == ENOENT ? "the engine executable is not installed"
                             : "the engine could not be started");
  }
  return std::make_unique<StdioEngineTransport>(pid, in_pipe[1], out_pipe[0]);
}

}  // namespace paiza
