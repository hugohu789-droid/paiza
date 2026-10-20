// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid
//
// A CandidateSource backed by an engine running in another process
// (docs/engine-contract.md). Nothing here is specific to one engine: which
// executable to run, which protocol it must speak and what its output may
// contain are all declared by the input profile.
//
// Two things this class is responsible for, beyond relaying:
//
//   * Keystroke efficiency. The engine keeps a session, so the source diffs
//     the Latin input against what the session already holds and sends one
//     TYPE (or UNTYPE) per keystroke instead of re-sending the whole word.
//     A keystroke therefore costs one round trip and one layer of splicing.
//
//   * Treating the engine as untrusted. Its candidates are committed into the
//     user's document, so every one is validated before it is offered, and a
//     failing engine is dropped rather than waited on.

#ifndef PAIZA_CORE_EXTERNAL_ENGINE_SOURCE_H_
#define PAIZA_CORE_EXTERNAL_ENGINE_SOURCE_H_

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "core/candidate_source.h"
#include "core/engine_transport.h"
#include "core/input_profile.h"

namespace paiza {

// Protocol versions this host implements. The profile names the protocol; the
// version is checked here, because the host is what knows which it speaks.
inline constexpr int kEngineProtocolVersion = 1;

// Caps applied to whatever the engine returns.
inline constexpr size_t kEngineMaxCandidates = 64;
inline constexpr size_t kEngineMaxCandidateChars = 128;

// True when `text` is safe to commit into a document: valid as given, free of
// control characters and bidirectional overrides, within the length cap, and
// inside the profile's declared code-point range when it declares one.
// Exposed for testing; ExternalEngineSource applies it to every candidate.
bool IsAcceptableCandidate(const std::wstring& text, const InputProfile& profile);

class ExternalEngineSource : public CandidateSource {
 public:
  // Performs the HELLO handshake. A transport that fails, an unexpected
  // protocol name or an unsupported version leaves the source permanently
  // unavailable rather than half-working.
  ExternalEngineSource(InputProfile profile,
                       std::unique_ptr<EngineTransport> transport);
  ~ExternalEngineSource() override;

  // CandidateSource -----------------------------------------------------
  std::wstring name() const override;
  int priority() const override;
  bool IsAvailable() const override;
  std::vector<Candidate> Query(const std::string& latin_input,
                               size_t max_results) override;

  // Profile gating, as on the other sources: only the active profile's
  // sources answer queries.
  void set_enabled(bool enabled) { enabled_.store(enabled); }
  bool enabled() const { return enabled_.load(); }

  // Why the engine is unavailable, for logs and the settings page. Empty
  // while it is working.
  const std::string& failure() const { return failure_; }

 private:
  // One request/response. Returns false and kills the engine on transport
  // failure, an ERR response, or a response that is not a well-formed OK.
  bool Exchange(const std::string& request, std::vector<std::string>* fields);
  void Fail(std::string reason);
  // Brings the engine's session in line with `latin`, returning the
  // candidates for it. Empty on any failure.
  std::vector<std::string> Sync(const std::string& latin, size_t max_results);

  InputProfile profile_;
  std::unique_ptr<EngineTransport> transport_;
  std::atomic<bool> enabled_{true};
  std::string failure_;
  // The Latin input the engine's session currently holds.
  std::string session_latin_;
};

}  // namespace paiza

#endif  // PAIZA_CORE_EXTERNAL_ENGINE_SOURCE_H_
