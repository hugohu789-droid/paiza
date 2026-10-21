// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid
//
// How ExternalEngineSource talks to an engine process. The protocol is
// transport-agnostic (docs/engine-contract.md): stdio on macOS and Linux, a
// named pipe on Windows, and a fake in tests.
//
// A transport owns the I/O concerns that must not reach the caller: the hard
// per-exchange timeout, the response size cap, and the process's death. All of
// them surface the same way, as a failed exchange, because the caller's
// response to any of them is the same -- stop using the engine.

#ifndef PAIZA_CORE_ENGINE_TRANSPORT_H_
#define PAIZA_CORE_ENGINE_TRANSPORT_H_

#include <memory>
#include <string>
#include <string_view>

namespace paiza {

// Host-enforced limits. Deliberately constants rather than profile fields: a
// profile that could raise the timeout would reintroduce the UI hang the
// timeout exists to prevent (docs/engine-contract.md §4).
inline constexpr int kEngineTimeoutMs = 200;
inline constexpr size_t kEngineMaxResponseBytes = 64 * 1024;

class EngineTransport {
 public:
  virtual ~EngineTransport() = default;

  // Sends one request line (without a terminator) and reads one response line
  // into `response` (without its terminator).
  //
  // Returns false when the exchange did not complete for any reason: the
  // timeout elapsed, the process died, the response exceeded
  // kEngineMaxResponseBytes, or the pipe failed. A transport that has returned
  // false is dead and must keep returning false; it is never retried, because
  // retrying a wedged engine on the typing path is how an IME hangs.
  virtual bool Exchange(std::string_view request, std::string* response) = 0;

  // False once the transport has failed. Never returns true again.
  virtual bool alive() const = 0;
};

// Framing, shared by the real transports. Exposed so it can be tested on
// every platform rather than only where its transport compiles.
enum class LineStatus {
  kComplete,    // a line was moved out of the buffer
  kIncomplete,  // no terminator yet; read more
  kOverflow,    // longer than kEngineMaxResponseBytes: a protocol error
};

// Moves the first LF-terminated line out of `buffer` into `line`, dropping the
// terminator and a CR before it. `buffer` keeps whatever followed.
LineStatus TakeLine(std::string* buffer, std::string* line);

// Spawns `path` (already resolved; see core/engine_path.h) and returns a
// transport speaking to its stdin and stdout. Returns nullptr with *reason set
// when the process could not be started.
//
// There is one of these per platform -- anonymous pipes and poll() on
// macOS and Linux, overlapped named pipes on Windows -- chosen by the build
// rather than by an #ifdef at the call site. The engine sees plain stdio
// either way, so a conforming engine needs no platform-specific code.
std::unique_ptr<EngineTransport> SpawnEngineTransport(const std::wstring& path,
                                                      const std::wstring& args,
                                                      std::string* reason);

}  // namespace paiza

#endif  // PAIZA_CORE_ENGINE_TRANSPORT_H_
