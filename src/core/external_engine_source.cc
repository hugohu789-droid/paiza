// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid

#include "core/external_engine_source.h"

#include <algorithm>

#include "common/string_utils.h"
#include "core/engine_path.h"

namespace paiza {
namespace {

constexpr char kTab = '\t';

// Splits a response line on TAB. The protocol guarantees no field contains
// one, so this is lossless.
std::vector<std::string> SplitFields(const std::string& line) {
  std::vector<std::string> fields;
  size_t begin = 0;
  while (true) {
    const size_t tab = line.find(kTab, begin);
    if (tab == std::string::npos) {
      fields.push_back(line.substr(begin));
      return fields;
    }
    fields.push_back(line.substr(begin, tab - begin));
    begin = tab + 1;
  }
}

// A control character in an outbound field would break the line framing, so
// such a request is never sent. Rejected rather than stripped: stripping
// turns an injection attempt into a valid-looking request.
bool HasControlChar(std::string_view text) {
  for (const char c : text) {
    if (static_cast<unsigned char>(c) < 0x20) return true;
  }
  return false;
}

// Characters that must never reach a document from an untrusted source:
// controls, and the bidirectional overrides that let text misrepresent itself.
bool IsForbiddenInDocument(wchar_t c) {
  const auto u = static_cast<unsigned long>(c);
  if (u < 0x20 || u == 0x7F) return true;
  if (u >= 0x202A && u <= 0x202E) return true;  // LRE..RLO, PDF
  if (u >= 0x2066 && u <= 0x2069) return true;  // LRI..PDI
  return false;
}

}  // namespace

bool IsAcceptableCandidate(const std::wstring& text,
                           const InputProfile& profile) {
  if (text.empty()) return false;
  if (text.size() > kEngineMaxCandidateChars) return false;
  for (const wchar_t c : text) {
    if (IsForbiddenInDocument(c)) return false;
    if (profile.engine_codepoints_set) {
      const auto u = static_cast<char32_t>(c);
      if (u < profile.engine_codepoint_low ||
          u > profile.engine_codepoint_high) {
        return false;
      }
    }
  }
  return true;
}

std::unique_ptr<ExternalEngineSource> MakeEngineSource(
    const InputProfile& profile, const std::wstring& install_dir,
    std::string* reason) {
  const auto fail = [reason](const std::string& text) {
    if (reason != nullptr) *reason = text;
    return std::unique_ptr<ExternalEngineSource>();
  };
  if (reason != nullptr) reason->clear();
  if (!profile.has_engine()) return nullptr;  // nothing declared; not an error

  // Without this the handshake would compare the engine's reported name
  // against an empty string and fail with a confusing message.
  if (profile.engine_protocol.empty()) {
    return fail("profile '" + profile.id +
                "' declares an engine but no engine_protocol");
  }

  std::string why;
  const std::wstring path =
      ResolveEnginePath(install_dir, profile.engine, &why);
  if (path.empty()) return fail(why);

  auto transport = SpawnEngineTransport(path, profile.engine_args, &why);
  if (transport == nullptr) return fail(why);

  // The handshake happens in the constructor, so this is where a wrong binary
  // or an unsupported protocol version is caught -- at startup, where it can
  // be reported, rather than on the user's first keystroke.
  auto source =
      std::make_unique<ExternalEngineSource>(profile, std::move(transport));
  if (!source->IsAvailable()) return fail(source->failure());
  return source;
}

ExternalEngineSource::ExternalEngineSource(
    InputProfile profile, std::unique_ptr<EngineTransport> transport)
    : profile_(std::move(profile)), transport_(std::move(transport)) {
  if (transport_ == nullptr) {
    Fail("no transport");
    return;
  }
  // Handshake first: an engine that is the wrong binary, or speaks a protocol
  // this host does not implement, must fail here rather than on a keystroke.
  std::vector<std::string> fields;
  if (!Exchange("HELLO", &fields)) return;
  if (fields.size() < 3) {
    Fail("HELLO did not report a name and version");
    return;
  }
  if (fields[1] != profile_.engine_protocol) {
    Fail("engine speaks " + fields[1] + ", profile expects " +
         profile_.engine_protocol);
    return;
  }
  if (fields[2] != std::to_string(kEngineProtocolVersion)) {
    Fail("unsupported protocol version " + fields[2]);
    return;
  }
}

ExternalEngineSource::~ExternalEngineSource() {
  // Best effort: let the engine exit cleanly. A failure here is irrelevant,
  // the process is going away either way.
  if (transport_ != nullptr && transport_->alive() && failure_.empty()) {
    std::string unused;
    transport_->Exchange("QUIT", &unused);
  }
}

std::wstring ExternalEngineSource::name() const {
  return L"engine:" + Utf8ToWide(profile_.id);
}

int ExternalEngineSource::priority() const {
  // Default below the compiled lexicon, so an engine supplements the
  // dictionary rather than displacing it.
  return profile_.engine_priority != 0 ? profile_.engine_priority : 90;
}

bool ExternalEngineSource::IsAvailable() const {
  return enabled_.load() && failure_.empty() && transport_ != nullptr &&
         transport_->alive();
}

void ExternalEngineSource::Fail(std::string reason) {
  // First failure wins: the original reason is the useful one.
  if (failure_.empty()) failure_ = std::move(reason);
  session_latin_.clear();
}

bool ExternalEngineSource::Exchange(const std::string& request,
                                   std::vector<std::string>* fields) {
  if (transport_ == nullptr || !transport_->alive()) {
    Fail("transport is not alive");
    return false;
  }
  if (HasControlChar(request)) {
    Fail("refused to send a request containing a control character");
    return false;
  }
  std::string response;
  if (!transport_->Exchange(request, &response)) {
    // Timeout, death or an oversized response: all mean stop using it.
    Fail("engine did not answer");
    return false;
  }
  *fields = SplitFields(response);
  if (fields->empty() || (*fields)[0] != "OK") {
    const std::string detail =
        fields->size() > 1 ? (*fields)[1] : std::string("malformed response");
    Fail("engine rejected a request: " + detail);
    return false;
  }
  return true;
}

std::vector<std::string> ExternalEngineSource::Sync(const std::string& latin,
                                                    size_t max_results) {
  const std::string cap = std::to_string(
      std::min(max_results == 0 ? kEngineMaxCandidates : max_results,
               kEngineMaxCandidates));
  std::vector<std::string> fields;

  // The session is a prefix of the new input: send only what was added. This
  // is what makes a keystroke cost one round trip.
  const bool appending = latin.size() > session_latin_.size() &&
                         latin.compare(0, session_latin_.size(),
                                       session_latin_) == 0;
  // The new input is a prefix of the session: the user deleted from the end.
  const bool deleting = latin.size() < session_latin_.size() &&
                        session_latin_.compare(0, latin.size(), latin) == 0;

  if (!appending && !deleting && latin != session_latin_) {
    // Diverged (an edit that was not at the end): start the session over.
    if (!Exchange("CLEAR 0", &fields)) return {};
    session_latin_.clear();
  }

  if (deleting) {
    for (size_t i = session_latin_.size(); i > latin.size(); --i) {
      if (!Exchange("UNTYPE 0 fuzzy " + cap, &fields)) return {};
    }
    session_latin_ = latin;
    return fields;
  }

  if (latin == session_latin_) {
    // Nothing changed, but the caller still wants the list (a different cap,
    // or a repeated query).
    if (latin.empty()) return {};
    if (!Exchange("CAND 0 fuzzy " + cap, &fields)) return {};
    return fields;
  }

  for (size_t i = session_latin_.size(); i < latin.size(); ++i) {
    const std::string key(1, latin[i]);
    if (!Exchange("TYPE 0 " + key + " fuzzy " + cap, &fields)) return {};
  }
  session_latin_ = latin;
  return fields;
}

std::vector<Candidate> ExternalEngineSource::Query(
    const std::string& latin_input, size_t max_results) {
  if (!IsAvailable()) return {};
  if (latin_input.empty()) {
    if (!session_latin_.empty()) {
      std::vector<std::string> fields;
      if (Exchange("CLEAR 0", &fields)) session_latin_.clear();
    }
    return {};
  }
  if (HasControlChar(latin_input)) {
    // Not an engine fault, so the engine is left alive; this query simply
    // yields nothing.
    return {};
  }

  const std::vector<std::string> fields = Sync(latin_input, max_results);
  if (fields.empty()) return {};

  std::vector<Candidate> candidates;
  const size_t limit =
      std::min(max_results == 0 ? kEngineMaxCandidates : max_results,
               kEngineMaxCandidates);
  // fields[0] is "OK"; the rest are candidates. Surplus is discarded even if
  // the engine ignored the cap it was given.
  for (size_t i = 1; i < fields.size() && candidates.size() < limit; ++i) {
    const std::wstring text = Utf8ToWide(fields[i]);
    if (!IsAcceptableCandidate(text, profile_)) continue;
    Candidate candidate;
    candidate.text = text;
    candidate.comment = name();
    // Ordered by the engine; keep that order by decreasing score.
    candidate.score = static_cast<int32_t>(limit - candidates.size());
    candidates.push_back(std::move(candidate));
  }
  return candidates;
}

}  // namespace paiza
