// Copyright 2026 Paiza. All rights reserved.

#include "ipc/named_pipe_server.h"

#include <sddl.h>

#include "common/logging.h"
#include "ipc/named_pipe_client.h"  // kPipeName

namespace paiza::ipc {
namespace {

constexpr DWORD kBufferSize = 64 * 1024;

// Security descriptor for the pipe:
//   D:(A;;GA;;;WD)   - allow Everyone generic all (local pipe, protocol is
//                      validated and read-only w.r.t. the service state)
//   (A;;GA;;;AC)     - allow ALL APPLICATION PACKAGES (UWP AppContainer)
//   S:(ML;;NW;;;LW)  - low integrity label so sandboxed browser processes
//                      hosting the TSF DLL may connect
constexpr wchar_t kPipeSddl[] = L"D:(A;;GA;;;WD)(A;;GA;;;AC)S:(ML;;NW;;;LW)";

}  // namespace

NamedPipeServer::NamedPipeServer(Handler handler)
    : handler_(std::move(handler)) {
  ::InitializeCriticalSection(&workers_lock_);
}

NamedPipeServer::~NamedPipeServer() {
  Stop();
  ::DeleteCriticalSection(&workers_lock_);
}

HANDLE NamedPipeServer::CreatePipeInstance() {
  SECURITY_ATTRIBUTES attributes = {};
  attributes.nLength = sizeof(attributes);
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  if (!::ConvertStringSecurityDescriptorToSecurityDescriptorW(
          kPipeSddl, SDDL_REVISION_1, &descriptor, nullptr)) {
    PAIZA_LOG_ERROR(L"Failed to build pipe security descriptor");
    return INVALID_HANDLE_VALUE;
  }
  attributes.lpSecurityDescriptor = descriptor;

  HANDLE pipe = ::CreateNamedPipeW(
      kPipeName,
      PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
      PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT |
          PIPE_REJECT_REMOTE_CLIENTS,
      PIPE_UNLIMITED_INSTANCES, kBufferSize, kBufferSize, 0, &attributes);
  ::LocalFree(descriptor);
  return pipe;
}

bool NamedPipeServer::Start() {
  if (running_.exchange(true)) return true;

  // Fail fast if the pipe name cannot be created at all.
  HANDLE probe = CreatePipeInstance();
  if (probe == INVALID_HANDLE_VALUE) {
    running_ = false;
    return false;
  }
  ::CloseHandle(probe);

  stop_event_ = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
  acceptor_ = std::thread([this] { AcceptLoop(); });
  PAIZA_LOG_INFO(L"Pipe server started");
  return true;
}

void NamedPipeServer::Stop() {
  if (!running_.exchange(false)) return;
  ::SetEvent(stop_event_);
  if (acceptor_.joinable()) acceptor_.join();

  ::EnterCriticalSection(&workers_lock_);
  std::vector<std::thread> workers = std::move(workers_);
  ::LeaveCriticalSection(&workers_lock_);
  for (std::thread& worker : workers) {
    if (worker.joinable()) worker.join();
  }
  ::CloseHandle(stop_event_);
  stop_event_ = nullptr;
}

void NamedPipeServer::AcceptLoop() {
  while (running_) {
    HANDLE pipe = CreatePipeInstance();
    if (pipe == INVALID_HANDLE_VALUE) {
      ::Sleep(500);
      continue;
    }

    OVERLAPPED overlapped = {};
    overlapped.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    BOOL connected = ::ConnectNamedPipe(pipe, &overlapped);
    const DWORD error = ::GetLastError();
    if (!connected && error == ERROR_IO_PENDING) {
      HANDLE waits[2] = {overlapped.hEvent, stop_event_};
      const DWORD wait = ::WaitForMultipleObjects(2, waits, FALSE, INFINITE);
      if (wait != WAIT_OBJECT_0) {  // Stop requested.
        ::CancelIoEx(pipe, &overlapped);
        DWORD ignored = 0;
        ::GetOverlappedResult(pipe, &overlapped, &ignored, TRUE);
        ::CloseHandle(overlapped.hEvent);
        ::CloseHandle(pipe);
        break;
      }
      DWORD ignored = 0;
      connected =
          ::GetOverlappedResult(pipe, &overlapped, &ignored, FALSE);
    } else if (!connected && error == ERROR_PIPE_CONNECTED) {
      connected = TRUE;
    }
    ::CloseHandle(overlapped.hEvent);

    if (!connected) {
      ::CloseHandle(pipe);
      continue;
    }
    ::EnterCriticalSection(&workers_lock_);
    workers_.emplace_back([this, pipe] { ServeClient(pipe); });
    ::LeaveCriticalSection(&workers_lock_);
  }
}

void NamedPipeServer::ServeClient(HANDLE pipe) {
  std::vector<uint8_t> buffer(sizeof(MessageHeader) + kMaxPayloadBytes);
  while (running_) {
    DWORD read = 0;
    OVERLAPPED overlapped = {};
    overlapped.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    BOOL ok = ::ReadFile(pipe, buffer.data(),
                         static_cast<DWORD>(buffer.size()), &read,
                         &overlapped);
    if (!ok && ::GetLastError() == ERROR_IO_PENDING) {
      HANDLE waits[2] = {overlapped.hEvent, stop_event_};
      const DWORD wait = ::WaitForMultipleObjects(2, waits, FALSE, INFINITE);
      if (wait != WAIT_OBJECT_0) {
        ::CancelIoEx(pipe, &overlapped);
        ::GetOverlappedResult(pipe, &overlapped, &read, TRUE);
        ::CloseHandle(overlapped.hEvent);
        break;
      }
      ok = ::GetOverlappedResult(pipe, &overlapped, &read, FALSE);
    }
    ::CloseHandle(overlapped.hEvent);
    if (!ok || read == 0) break;  // Client disconnected.

    const auto request = DecodeFrame(buffer.data(), read);
    if (!request.has_value()) {
      PAIZA_LOG_WARN(L"Dropping malformed IPC frame");
      break;
    }
    const Message reply = handler_(*request);
    const std::vector<uint8_t> frame = EncodeFrame(reply);
    DWORD written = 0;
    OVERLAPPED write_overlapped = {};
    write_overlapped.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    BOOL wrote = ::WriteFile(pipe, frame.data(),
                             static_cast<DWORD>(frame.size()), &written,
                             &write_overlapped);
    if (!wrote && ::GetLastError() == ERROR_IO_PENDING) {
      wrote = ::GetOverlappedResult(pipe, &write_overlapped, &written, TRUE);
    }
    ::CloseHandle(write_overlapped.hEvent);
    if (!wrote || written != frame.size()) break;
  }
  ::FlushFileBuffers(pipe);
  ::DisconnectNamedPipe(pipe);
  ::CloseHandle(pipe);
}

}  // namespace paiza::ipc
