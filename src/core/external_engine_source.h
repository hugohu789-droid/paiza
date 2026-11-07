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
//
// What kind of answer the engine gives is the profile's `engine_mode`, passed
// through unchanged: the host does not interpret it, so an engine may offer
// modes this build has never heard of. The one thing the host does need to
// know is whether the items carry metrics after the text, which the profile
// states with `engine_scored`; then the engine's own score is what ranks the
// candidates, instead of their position in the list.

#ifndef PAIZA_CORE_EXTERNAL_ENGINE_SOURCE_H_
#define PAIZA_CORE_EXTERNAL_ENGINE_SOURCE_H_

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/candidate_source.h"
#include "core/engine_transport.h"
#include "core/input_profile.h"
#include "core/user_dictionary.h"

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

// One item of a scored response: the candidate text, then the metrics behind
// the engine's own ranking, as "text,key:value,key:value...". `text` ends at
// the first comma.
struct ScoredItem {
  std::string text;
  // The engine's `score:` field, 0 when the item carried none. Relative
  // within one source, which is all the merge uses it for.
  double score = 0;
  bool scored = false;
};

// Splits one scored item. An item with no comma is all text and no metrics,
// which is how a mode that was declared scored but answers plainly still
// yields a usable candidate instead of an empty one. Exposed for testing.
ScoredItem ParseScoredItem(const std::string& item);

class ExternalEngineSource;

// Builds the engine source a profile declares: resolves the executable inside
// `install_dir` (core/engine_path.h), spawns it, and performs the handshake.
//
// Returns nullptr in two different situations, which the caller tells apart by
// whether *reason is empty:
//
//   empty reason   the profile declares no engine. Not an error; it is how
//                  every profile shipped so far behaves.
//   with a reason  it declares one that could not be used -- the name was
//                  rejected, the file is not installed, or the handshake
//                  failed. Worth logging and showing in the settings page.
//
// Nothing half-working is ever returned, so a caller that gets a source has
// one that answered HELLO correctly.
std::unique_ptr<ExternalEngineSource> MakeEngineSource(
    const InputProfile& profile, const std::wstring& install_dir,
    std::string* reason);

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

  // The user's own words, handed to the engine so that it can rank them:
  // the host stores them, the engine decides where they go in the list,
  // which is the only place that decision can be made consistently with the
  // engine's own scoring. No-ops unless the profile sets
  // `engine_user_words`, because an engine is not obliged to understand
  // them -- and if one rejects them anyway, that is not treated as a
  // failure: the words are simply dropped and nothing further is sent.
  //
  // Times are Unix seconds here and whole days on the wire, which is the
  // unit the engine's decay half-life is expressed in.
  void ReplayUserWords(const std::vector<UserWordUse>& words);
  void LearnCommitted(const std::wstring& word, int64_t unix_seconds);
  bool user_words_enabled() const { return user_words_ok_; }

  // Profile gating, as on the other sources: only the active profile's
  // sources answer queries.
  void set_enabled(bool enabled) { enabled_.store(enabled); }
  bool enabled() const { return enabled_.load(); }

  // Why the engine stopped working. Empty while it is working, and empty when
  // it is merely disabled because another profile is active -- being gated is
  // not a failure. Reached through ProfileSwitch::active_engine_error(), which
  // also covers an engine that never started.
  const std::string& failure() const { return failure_; }

 private:
  // One request/response. Returns false and kills the engine on transport
  // failure, an ERR response, or a response that is not a well-formed OK.
  bool Exchange(const std::string& request, std::vector<std::string>* fields);
  void Fail(std::string reason);
  // Brings the engine's session in line with `latin`, returning the
  // candidates for it. Empty on any failure.
  std::vector<std::string> Sync(const std::string& latin, size_t max_results);
  // The <mode> field this profile asks for, never empty.
  const std::string& mode() const;

  InputProfile profile_;
  std::unique_ptr<EngineTransport> transport_;
  std::atomic<bool> enabled_{true};
  std::string failure_;
  // One request/response that is allowed to be refused: an engine that does
  // not implement the user-word commands says ERR, and that is an answer,
  // not a fault. Returns false on a refusal without killing the engine.
  bool TryExchange(const std::string& request);
  // Unix seconds -> the engine's tick, whole days.
  static std::string Tick(int64_t unix_seconds);

  // The Latin input the engine's session currently holds.
  std::string session_latin_;
  // False once the engine has refused a user-word command, or when the
  // profile never asked for them.
  bool user_words_ok_ = false;
};

}  // namespace paiza

#endif  // PAIZA_CORE_EXTERNAL_ENGINE_SOURCE_H_
