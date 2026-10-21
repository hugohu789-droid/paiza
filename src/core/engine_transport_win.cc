// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid
//
// SpawnEngineTransport on Windows.
//
// The engine still speaks plain stdio: it reads stdin and writes stdout,
// exactly as it does on macOS and Linux, so a conforming engine needs no
// Windows-specific code. What differs is only what those handles are made of.
// Here they are the client ends of two named pipes whose host ends are
// overlapped, because an *anonymous* pipe cannot be read with a timeout --
// ReadFile on one blocks until data arrives or the writer closes, with no way
// to abandon the wait. Overlapped named pipes give the hard per-exchange
// timeout the contract requires, which matters most on Windows: the text
// service is loaded into arbitrary host applications, including sandboxed
// browser renderers, so a wedged engine must never reach a host's UI thread.
//
// Compiled only on Windows (see CMakeLists.txt), so there is no #ifdef here
// and no platform branch at the call site.

#include <windows.h>

#include <atomic>
#include <string>

#include "core/engine_transport.h"

namespace paiza {
namespace {

constexpr DWORD kPipeBufferBytes = 64 * 1024;

std::wstring UniquePipeName(const wchar_t* role) {
  static std::atomic<unsigned> counter{0};
  return std::wstring(L"\\\\.\\pipe\\paiza-engine-") + role + L"-" +
         std::to_wstring(GetCurrentProcessId()) + L"-" +
         std::to_wstring(counter.fetch_add(1));
}

// Creates one pipe and returns both ends. `host_access` is PIPE_ACCESS_INBOUND
// or PIPE_ACCESS_OUTBOUND; the client end gets the opposite direction and is
// inheritable, because it becomes one of the child's standard handles.
bool CreatePipePair(const wchar_t* role, DWORD host_access, HANDLE* host,
                    HANDLE* client) {
  const std::wstring name = UniquePipeName(role);
  // FIRST_PIPE_INSTANCE is the security-relevant flag: without it, a process
  // that had pre-created a pipe of this name would receive the handshake
  // instead of our child. With it, creation fails and we refuse to start.
  *host = CreateNamedPipeW(
      name.c_str(),
      host_access | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
      PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT |
          PIPE_REJECT_REMOTE_CLIENTS,
      /*nMaxInstances=*/1, kPipeBufferBytes, kPipeBufferBytes, 0, nullptr);
  if (*host == INVALID_HANDLE_VALUE) return false;

  SECURITY_ATTRIBUTES sa = {};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;
  const DWORD client_access =
      host_access == PIPE_ACCESS_INBOUND ? GENERIC_WRITE : GENERIC_READ;
  *client = CreateFileW(name.c_str(), client_access, 0, &sa, OPEN_EXISTING,
                        0, nullptr);
  if (*client == INVALID_HANDLE_VALUE) {
    CloseHandle(*host);
    *host = INVALID_HANDLE_VALUE;
    return false;
  }
  // The instance is connected by the CreateFileW above, so ConnectNamedPipe
  // is unnecessary; and with nMaxInstances 1 no second client can connect.
  return true;
}

class PipeEngineTransport : public EngineTransport {
 public:
  PipeEngineTransport(HANDLE process, HANDLE to_child, HANDLE from_child)
      : process_(process), to_child_(to_child), from_child_(from_child) {
    read_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    write_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (read_event_ == nullptr || write_event_ == nullptr) Die();
  }

  ~PipeEngineTransport() override {
    ClosePipes();
    if (process_ != nullptr) {
      // The caller sends QUIT first and closing the child's stdin ends it in
      // any case, so give it a moment, then insist. A wedged engine must not
      // be left running against the user's dictionary files.
      if (WaitForSingleObject(process_, 100) != WAIT_OBJECT_0) {
        TerminateProcess(process_, 1);
        WaitForSingleObject(process_, 100);
      }
      CloseHandle(process_);
    }
    if (read_event_ != nullptr) CloseHandle(read_event_);
    if (write_event_ != nullptr) CloseHandle(write_event_);
  }

  bool alive() const override { return alive_; }

  bool Exchange(std::string_view request, std::string* response) override {
    if (!alive_) return false;
    std::string line(request);
    line.push_back('\n');
    if (!WriteAll(line)) return Die();

    // One deadline for the whole exchange, computed once: an engine that
    // dribbles bytes out must not be able to extend it by staying just
    // active enough to reset a per-read timeout.
    const ULONGLONG deadline = GetTickCount64() + kEngineTimeoutMs;
    while (true) {
      switch (TakeLine(&buffer_, response)) {
        case LineStatus::kComplete:
          return true;
        case LineStatus::kOverflow:
          return Die();
        case LineStatus::kIncomplete:
          break;
      }
      const ULONGLONG now = GetTickCount64();
      if (now >= deadline) return Die();
      char chunk[4096];
      DWORD read = 0;
      if (!ReadOnce(chunk, sizeof(chunk), static_cast<DWORD>(deadline - now),
                    &read)) {
        return Die();
      }
      if (read == 0) return Die();  // the engine closed stdout
      buffer_.append(chunk, read);
    }
  }

 private:
  // Reads into `chunk`, waiting at most `timeout_ms`. False on timeout, EOF
  // or any pipe error -- all of which mean the same thing to the caller.
  bool ReadOnce(char* chunk, DWORD capacity, DWORD timeout_ms, DWORD* read) {
    OVERLAPPED ov = {};
    ov.hEvent = read_event_;
    ResetEvent(read_event_);
    if (ReadFile(from_child_, chunk, capacity, read, &ov)) return *read > 0;
    if (GetLastError() != ERROR_IO_PENDING) return false;
    if (WaitForSingleObject(read_event_, timeout_ms) != WAIT_OBJECT_0) {
      CancelIoEx(from_child_, &ov);
      // Waiting here is not optional: until the cancelled read completes the
      // kernel may still write into `chunk`, which lives on the caller's
      // stack and is about to go away.
      GetOverlappedResult(from_child_, &ov, read, TRUE);
      return false;
    }
    return GetOverlappedResult(from_child_, &ov, read, FALSE) && *read > 0;
  }

  bool WriteAll(const std::string& data) {
    size_t written = 0;
    const ULONGLONG deadline = GetTickCount64() + kEngineTimeoutMs;
    while (written < data.size()) {
      const ULONGLONG now = GetTickCount64();
      if (now >= deadline) return false;
      OVERLAPPED ov = {};
      ov.hEvent = write_event_;
      ResetEvent(write_event_);
      DWORD wrote = 0;
      const DWORD chunk = static_cast<DWORD>(data.size() - written);
      if (!WriteFile(to_child_, data.data() + written, chunk, &wrote, &ov)) {
        if (GetLastError() != ERROR_IO_PENDING) return false;
        if (WaitForSingleObject(write_event_,
                                static_cast<DWORD>(deadline - now)) !=
            WAIT_OBJECT_0) {
          CancelIoEx(to_child_, &ov);
          GetOverlappedResult(to_child_, &ov, &wrote, TRUE);
          return false;
        }
        if (!GetOverlappedResult(to_child_, &ov, &wrote, FALSE)) return false;
      }
      if (wrote == 0) return false;
      written += wrote;
    }
    return true;
  }

  // Latches the failure. Timeout, EOF, an oversized line and a broken pipe
  // all arrive here, because the caller's response to each is the same.
  bool Die() {
    alive_ = false;
    ClosePipes();
    return false;
  }

  void ClosePipes() {
    if (to_child_ != INVALID_HANDLE_VALUE) CloseHandle(to_child_);
    if (from_child_ != INVALID_HANDLE_VALUE) CloseHandle(from_child_);
    to_child_ = from_child_ = INVALID_HANDLE_VALUE;
  }

  HANDLE process_ = nullptr;
  HANDLE to_child_ = INVALID_HANDLE_VALUE;
  HANDLE from_child_ = INVALID_HANDLE_VALUE;
  HANDLE read_event_ = nullptr;
  HANDLE write_event_ = nullptr;
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

  HANDLE to_child = INVALID_HANDLE_VALUE;    // host writes: the child's stdin
  HANDLE child_stdin = INVALID_HANDLE_VALUE;
  HANDLE from_child = INVALID_HANDLE_VALUE;  // host reads: the child's stdout
  HANDLE child_stdout = INVALID_HANDLE_VALUE;
  if (!CreatePipePair(L"in", PIPE_ACCESS_OUTBOUND, &to_child, &child_stdin)) {
    return fail("could not create a pipe for the engine");
  }
  if (!CreatePipePair(L"out", PIPE_ACCESS_INBOUND, &from_child,
                      &child_stdout)) {
    CloseHandle(to_child);
    CloseHandle(child_stdin);
    return fail("could not create a pipe for the engine");
  }

  // The host is a windowed process with no console, so there is nowhere for
  // the engine's stderr to go. It must not share stdout -- diagnostics in the
  // protocol stream would corrupt the framing -- so it is discarded, and the
  // contract gives engines `ERR` for reporting problems instead.
  SECURITY_ATTRIBUTES sa = {};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;
  HANDLE child_stderr = CreateFileW(L"NUL", GENERIC_WRITE, 0, &sa,
                                    OPEN_EXISTING, 0, nullptr);

  STARTUPINFOW si = {};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = child_stdin;
  si.hStdOutput = child_stdout;
  si.hStdError = child_stderr;

  // The path was validated to hold no quote (core/engine_path.h), so quoting
  // it cannot be escaped out of.
  std::wstring command_line = L"\"" + path + L"\"";
  if (!args.empty()) command_line += L" " + args;
  std::wstring mutable_command_line = command_line;  // CreateProcessW writes

  PROCESS_INFORMATION pi = {};
  const BOOL ok = CreateProcessW(path.c_str(), mutable_command_line.data(),
                                 nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                                 nullptr, nullptr, &si, &pi);
  const DWORD error = ok ? 0 : GetLastError();
  // The child holds its own copies now; the host's must go, or an EOF from a
  // dead engine would never arrive.
  CloseHandle(child_stdin);
  CloseHandle(child_stdout);
  if (child_stderr != INVALID_HANDLE_VALUE) CloseHandle(child_stderr);
  if (!ok) {
    CloseHandle(to_child);
    CloseHandle(from_child);
    return fail(error == ERROR_FILE_NOT_FOUND
                    ? "the engine executable is not installed"
                    : "the engine could not be started");
  }
  CloseHandle(pi.hThread);
  return std::make_unique<PipeEngineTransport>(pi.hProcess, to_child,
                                               from_child);
}

}  // namespace paiza
