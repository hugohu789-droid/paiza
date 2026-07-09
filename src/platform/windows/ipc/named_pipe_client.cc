// Copyright 2026 Paiza. All rights reserved.

#include "ipc/named_pipe_client.h"

#include <windows.h>

#include "common/logging.h"

namespace paiza::ipc {
namespace {

// RAII wrapper for HANDLE.
class ScopedHandle {
 public:
  explicit ScopedHandle(HANDLE handle = INVALID_HANDLE_VALUE)
      : handle_(handle) {}
  ~ScopedHandle() { Reset(); }
  ScopedHandle(const ScopedHandle&) = delete;
  ScopedHandle& operator=(const ScopedHandle&) = delete;

  void Reset(HANDLE handle = INVALID_HANDLE_VALUE) {
    if (IsValid()) ::CloseHandle(handle_);
    handle_ = handle;
  }
  bool IsValid() const {
    return handle_ != INVALID_HANDLE_VALUE && handle_ != nullptr;
  }
  HANDLE get() const { return handle_; }

 private:
  HANDLE handle_;
};

// Waits for an overlapped operation with a timeout. Cancels on timeout so
// the OVERLAPPED buffer is safe to reuse/destroy.
bool WaitOverlapped(HANDLE pipe, OVERLAPPED* overlapped, DWORD timeout_ms,
                    DWORD* bytes_transferred) {
  const DWORD wait = ::WaitForSingleObject(overlapped->hEvent, timeout_ms);
  if (wait != WAIT_OBJECT_0) {
    ::CancelIoEx(pipe, overlapped);
    // Wait for the cancellation to finish before the OVERLAPPED goes away.
    ::GetOverlappedResult(pipe, overlapped, bytes_transferred, TRUE);
    return false;
  }
  return ::GetOverlappedResult(pipe, overlapped, bytes_transferred, FALSE) !=
         FALSE;
}

bool WriteFrame(HANDLE pipe, const std::vector<uint8_t>& frame,
                DWORD timeout_ms) {
  OVERLAPPED overlapped = {};
  overlapped.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (overlapped.hEvent == nullptr) return false;

  DWORD written = 0;
  bool ok = ::WriteFile(pipe, frame.data(), static_cast<DWORD>(frame.size()),
                        &written, &overlapped) != FALSE;
  if (!ok && ::GetLastError() == ERROR_IO_PENDING) {
    ok = WaitOverlapped(pipe, &overlapped, timeout_ms, &written);
  }
  ::CloseHandle(overlapped.hEvent);
  return ok && written == frame.size();
}

bool ReadFrame(HANDLE pipe, std::vector<uint8_t>* frame, DWORD timeout_ms) {
  frame->resize(sizeof(MessageHeader) + kMaxPayloadBytes);
  OVERLAPPED overlapped = {};
  overlapped.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (overlapped.hEvent == nullptr) return false;

  DWORD read = 0;
  bool ok = ::ReadFile(pipe, frame->data(), static_cast<DWORD>(frame->size()),
                       &read, &overlapped) != FALSE;
  if (!ok && ::GetLastError() == ERROR_IO_PENDING) {
    ok = WaitOverlapped(pipe, &overlapped, timeout_ms, &read);
  }
  ::CloseHandle(overlapped.hEvent);
  if (!ok) return false;
  frame->resize(read);
  return true;
}

// Opens the pipe, retrying briefly while all instances are busy.
HANDLE OpenPipe(DWORD connect_timeout_ms) {
  const ULONGLONG deadline = ::GetTickCount64() + connect_timeout_ms;
  for (;;) {
    HANDLE pipe = ::CreateFileW(kPipeName, GENERIC_READ | GENERIC_WRITE, 0,
                                nullptr, OPEN_EXISTING,
                                FILE_FLAG_OVERLAPPED, nullptr);
    if (pipe != INVALID_HANDLE_VALUE) {
      DWORD mode = PIPE_READMODE_MESSAGE;
      if (!::SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr)) {
        ::CloseHandle(pipe);
        return INVALID_HANDLE_VALUE;
      }
      return pipe;
    }
    const DWORD error = ::GetLastError();
    const ULONGLONG now = ::GetTickCount64();
    if (now >= deadline) return INVALID_HANDLE_VALUE;
    if (error == ERROR_PIPE_BUSY) {
      if (!::WaitNamedPipeW(kPipeName,
                            static_cast<DWORD>(deadline - now))) {
        return INVALID_HANDLE_VALUE;
      }
    } else if (error == ERROR_FILE_NOT_FOUND) {
      // The server may still be creating its first pipe instance
      // (e.g. right after service start); retry briefly.
      ::Sleep(10);
    } else {
      return INVALID_HANDLE_VALUE;
    }
  }
}

}  // namespace

std::optional<Message> NamedPipeClient::Transact(const Message& request) {
  ScopedHandle pipe(OpenPipe(options_.connect_timeout_ms));
  if (!pipe.IsValid()) return std::nullopt;

  if (!WriteFrame(pipe.get(), EncodeFrame(request), options_.io_timeout_ms)) {
    return std::nullopt;
  }
  std::vector<uint8_t> reply;
  if (!ReadFrame(pipe.get(), &reply, options_.io_timeout_ms)) {
    return std::nullopt;
  }
  return DecodeFrame(reply.data(), reply.size());
}

bool NamedPipeClient::Send(const Message& request) {
  ScopedHandle pipe(OpenPipe(options_.connect_timeout_ms));
  if (!pipe.IsValid()) return false;
  return WriteFrame(pipe.get(), EncodeFrame(request), options_.io_timeout_ms);
}

}  // namespace paiza::ipc
