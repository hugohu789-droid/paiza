// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid

#include "core/external_engine_source.h"

#include <algorithm>
#include <cstdlib>

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

ScoredItem ParseScoredItem(const std::string& item) {
  ScoredItem out;
  const size_t comma = item.find(',');
  if (comma == std::string::npos) {
    out.text = item;
    return out;
  }
  out.text = item.substr(0, comma);
  // "...,score:0.9616858,..." -- the last field in practice, but found by
  // name rather than by position, so the order of the metrics is not part of
  // the contract.
  static constexpr char kKey[] = ",score:";
  const size_t at = item.find(kKey, comma);
  if (at == std::string::npos) return out;
  const size_t begin = at + sizeof(kKey) - 1;
  const size_t end = item.find(',', begin);
  const std::string number =
      item.substr(begin, end == std::string::npos ? std::string::npos
                                                  : end - begin);
  // strtod rather than stod: this runs inside a text service loaded into
  // other applications, and a malformed number is not worth an exception for.
  // An unparseable one leaves the item unscored, so it keeps its position in
  // the list and the candidate is still offered.
  char* parsed_end = nullptr;
  const double value = std::strtod(number.c_str(), &parsed_end);
  if (parsed_end != nullptr && *parsed_end == 0 && !number.empty()) {
    out.score = value;
    out.scored = true;
  }
  return out;
}

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
  // Allowed until the engine says otherwise, so that a commit reaches it
  // whether or not the host replayed a store first.
  user_words_ok_ = profile_.engine_user_words;
}

ExternalEngineSource::~ExternalEngineSource() {
  // Best effort: let the engine exit cleanly. A failure here is irrelevant,
  // the process is going away either way.
  if (transport_ != nullptr && transport_->alive() && failure_.empty()) {
    std::string unused;
    transport_->Exchange("QUIT", &unused);
  }
}

std::string ExternalEngineSource::Tick(int64_t unix_seconds) {
  // Whole days: the engine's habit half-life is in ticks, and a day is the
  // unit that makes "a word I used a month ago has faded" mean what it
  // says. Before the epoch is clamped to 0 rather than wrapping.
  const int64_t days = unix_seconds > 0 ? unix_seconds / 86400 : 0;
  return std::to_string(days);
}

bool ExternalEngineSource::TryExchange(const std::string& request) {
  if (!IsAvailable() || !user_words_ok_) return false;
  if (HasControlChar(request)) return false;
  std::string response;
  if (!transport_->Exchange(request, &response)) {
    // No answer at all is a real fault, unlike a refusal.
    Fail("engine did not answer");
    user_words_ok_ = false;
    return false;
  }
  const std::vector<std::string> fields = SplitFields(response);
  if (fields.empty() || fields[0] != "OK") {
    // The engine does not do this. Stop asking; it is still a good engine
    // for everything else (docs/engine-contract.md: an engine need only
    // implement HELLO, TYPE, UNTYPE and QUIT).
    user_words_ok_ = false;
    return false;
  }
  return true;
}

void ExternalEngineSource::ReplayUserWords(
    const std::vector<UserWordUse>& words) {
  if (!user_words_ok_ || !IsAvailable()) return;
  if (!TryExchange("USER clear")) return;
  for (const UserWordUse& use : words) {
    if (use.word.empty() || use.count == 0) continue;
    if (!TryExchange("USER restore " + std::to_string(use.count) + " " +
                     Tick(use.last_used) + " " + WideToUtf8(use.word))) {
      return;
    }
  }
}

void ExternalEngineSource::LearnCommitted(const std::wstring& word,
                                          int64_t unix_seconds) {
  if (word.empty() || !profile_.engine_user_words) return;
  TryExchange("USER learn " + Tick(unix_seconds) + " " + WideToUtf8(word));
}

std::wstring ExternalEngineSource::name() const {
  return L"engine:" + Utf8ToWide(profile_.id);
}

const std::string& ExternalEngineSource::mode() const {
  // A profile that names no mode gets the one every conforming engine must
  // implement, so the documented minimum engine keeps working.
  static const std::string kFallback = kDefaultEngineMode;
  return profile_.engine_mode.empty() ? kFallback : profile_.engine_mode;
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
      if (!Exchange("UNTYPE 0 " + mode() + " " + cap, &fields)) return {};
    }
    session_latin_ = latin;
    return fields;
  }

  if (latin == session_latin_) {
    // Nothing changed, but the caller still wants the list (a different cap,
    // or a repeated query).
    if (latin.empty()) return {};
    if (!Exchange("CAND 0 " + mode() + " " + cap, &fields)) return {};
    return fields;
  }

  for (size_t i = session_latin_.size(); i < latin.size(); ++i) {
    const std::string key(1, latin[i]);
    if (!Exchange("TYPE 0 " + key + " " + mode() + " " + cap, &fields)) {
      return {};
    }
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
    std::string item = fields[i];
    double engine_score = 0;
    bool scored = false;
    if (profile_.engine_scored) {
      const ScoredItem parsed = ParseScoredItem(item);
      item = parsed.text;
      engine_score = parsed.score;
      scored = parsed.scored;
    }
    const std::wstring text = Utf8ToWide(item);
    if (!IsAcceptableCandidate(text, profile_)) continue;
    Candidate candidate;
    candidate.text = text;
    candidate.comment = name();
    if (scored) {
      // The engine's own score, scaled to the integer the merge sorts on.
      // Scores from the ZCode engine run roughly 0..3, so the factor keeps
      // four decimal places; clamped because the number came from the engine
      // and a cast that overflows int32_t is undefined behaviour.
      const double scaled =
          std::min(std::max(engine_score * 10000.0, -1.0e9), 1.0e9);
      candidate.score = static_cast<int32_t>(scaled);
    } else {
      // Ordered by the engine; keep that order by decreasing score.
      candidate.score = static_cast<int32_t>(limit - candidates.size());
    }
    candidates.push_back(std::move(candidate));
  }
  return candidates;
}

}  // namespace paiza
