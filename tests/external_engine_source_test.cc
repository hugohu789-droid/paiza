// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid
//
// ExternalEngineSource drives an engine in another process, so the paths that
// matter most are the ones a healthy engine never takes: timeouts, death,
// malformed answers and hostile candidates. A fake transport makes all of
// them ordinary unit tests instead of things nobody ever runs.

#include "core/external_engine_source.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "core/engine_transport.h"
#include "core/input_profile.h"

namespace paiza {
namespace {

// A scriptable stand-in for a pipe to an engine. Records what was sent and
// replies from a queue; can be told to die, which is how a timeout, a crash
// and an oversized response all look to the source.
class FakeTransport : public EngineTransport {
 public:
  bool Exchange(std::string_view request, std::string* response) override {
    sent.emplace_back(request);
    if (!alive_) return false;
    if (die_after_ >= 0 && static_cast<int>(sent.size()) > die_after_) {
      alive_ = false;
      return false;
    }
    if (replies.empty()) {
      *response = default_reply;
      return true;
    }
    *response = replies.front();
    replies.erase(replies.begin());
    return true;
  }
  bool alive() const override { return alive_; }

  void DieAfter(int exchanges) { die_after_ = exchanges; }

  std::vector<std::string> sent;
  std::vector<std::string> replies;
  std::string default_reply = "OK";

 private:
  bool alive_ = true;
  int die_after_ = -1;
};

InputProfile EngineProfile() {
  InputProfile profile;
  profile.id = "zcode";
  profile.engine = L"zcode_stdio";
  profile.engine_protocol = "paiza-engine";
  return profile;
}

// The handshake every healthy engine answers first.
std::string Hello() { return "OK\tpaiza-engine\t1"; }

// Builds a source whose transport is scripted; the raw pointer stays valid
// for the source's lifetime.
std::unique_ptr<ExternalEngineSource> MakeSource(FakeTransport** out,
                                                 InputProfile profile) {
  auto transport = std::make_unique<FakeTransport>();
  *out = transport.get();
  if (transport->replies.empty()) transport->replies.push_back(Hello());
  return std::make_unique<ExternalEngineSource>(std::move(profile),
                                                std::move(transport));
}

// Scripts the handshake plus the given replies.
std::unique_ptr<ExternalEngineSource> MakeSourceWith(
    FakeTransport** out, std::vector<std::string> replies,
    InputProfile profile = EngineProfile()) {
  auto transport = std::make_unique<FakeTransport>();
  transport->replies.push_back(Hello());
  for (std::string& reply : replies) {
    transport->replies.push_back(std::move(reply));
  }
  *out = transport.get();
  return std::make_unique<ExternalEngineSource>(std::move(profile),
                                                std::move(transport));
}

// ---- handshake ---------------------------------------------------------

TEST(ExternalEngineSourceTest, HandshakeIsTheFirstThingSent) {
  FakeTransport* transport = nullptr;
  auto source = MakeSource(&transport, EngineProfile());
  ASSERT_FALSE(transport->sent.empty());
  EXPECT_EQ(transport->sent[0], "HELLO");
  EXPECT_TRUE(source->IsAvailable());
  EXPECT_TRUE(source->failure().empty());
}

TEST(ExternalEngineSourceTest, WrongProtocolNameDisablesTheEngine) {
  FakeTransport* transport = nullptr;
  auto t = std::make_unique<FakeTransport>();
  t->replies.push_back("OK\tsome-other-engine\t1");
  transport = t.get();
  ExternalEngineSource source(EngineProfile(), std::move(t));
  EXPECT_FALSE(source.IsAvailable());
  EXPECT_NE(source.failure().find("some-other-engine"), std::string::npos);
  (void)transport;
}

TEST(ExternalEngineSourceTest, UnsupportedVersionDisablesTheEngine) {
  auto t = std::make_unique<FakeTransport>();
  t->replies.push_back("OK\tpaiza-engine\t99");
  ExternalEngineSource source(EngineProfile(), std::move(t));
  EXPECT_FALSE(source.IsAvailable());
  EXPECT_NE(source.failure().find("99"), std::string::npos);
}

TEST(ExternalEngineSourceTest, TruncatedHandshakeDisablesTheEngine) {
  auto t = std::make_unique<FakeTransport>();
  t->replies.push_back("OK");  // No name, no version.
  ExternalEngineSource source(EngineProfile(), std::move(t));
  EXPECT_FALSE(source.IsAvailable());
}

TEST(ExternalEngineSourceTest, DeadTransportAtHandshakeDisablesTheEngine) {
  auto t = std::make_unique<FakeTransport>();
  t->DieAfter(0);  // Fails the very first exchange, like a timeout.
  ExternalEngineSource source(EngineProfile(), std::move(t));
  EXPECT_FALSE(source.IsAvailable());
  EXPECT_FALSE(source.failure().empty());
}

TEST(ExternalEngineSourceTest, NullTransportIsHandled) {
  ExternalEngineSource source(EngineProfile(), nullptr);
  EXPECT_FALSE(source.IsAvailable());
  EXPECT_TRUE(source.Query("sain", 9).empty());
}

// ---- incremental session ----------------------------------------------

TEST(ExternalEngineSourceTest, TypingSendsOneTypePerNewKey) {
  FakeTransport* t = nullptr;
  auto source = MakeSourceWith(&t, {"OK\tA", "OK\tAB", "OK\tABC"});
  source->Query("s", 9);
  source->Query("sa", 9);
  source->Query("sai", 9);

  // HELLO, then exactly one TYPE per keystroke.
  ASSERT_EQ(t->sent.size(), 4u);
  EXPECT_EQ(t->sent[1].substr(0, 9), "TYPE 0 s ");
  EXPECT_EQ(t->sent[2].substr(0, 9), "TYPE 0 a ");
  EXPECT_EQ(t->sent[3].substr(0, 9), "TYPE 0 i ");
}

TEST(ExternalEngineSourceTest, BackspaceSendsUntype) {
  FakeTransport* t = nullptr;
  auto source = MakeSourceWith(&t, {"OK\tA", "OK\tAB", "OK\tA"});
  source->Query("s", 9);
  source->Query("sa", 9);
  t->sent.clear();
  source->Query("s", 9);
  ASSERT_EQ(t->sent.size(), 1u);
  EXPECT_EQ(t->sent[0].substr(0, 11), "UNTYPE 0 fu");
}

TEST(ExternalEngineSourceTest, DivergentInputRestartsTheSession) {
  FakeTransport* t = nullptr;
  auto source = MakeSourceWith(&t, {"OK\tA", "OK\tAB", "OK", "OK\tM", "OK\tMO"});
  source->Query("sa", 9);  // Two TYPEs.
  t->sent.clear();
  source->Query("mo", 9);  // Shares no prefix: CLEAR, then retype.
  ASSERT_GE(t->sent.size(), 3u);
  EXPECT_EQ(t->sent[0], "CLEAR 0");
  EXPECT_EQ(t->sent[1].substr(0, 9), "TYPE 0 m ");
  EXPECT_EQ(t->sent[2].substr(0, 9), "TYPE 0 o ");
}

TEST(ExternalEngineSourceTest, EmptyInputClearsTheSession) {
  FakeTransport* t = nullptr;
  auto source = MakeSourceWith(&t, {"OK\tA", "OK"});
  source->Query("s", 9);
  t->sent.clear();
  EXPECT_TRUE(source->Query("", 9).empty());
  ASSERT_EQ(t->sent.size(), 1u);
  EXPECT_EQ(t->sent[0], "CLEAR 0");
}

TEST(ExternalEngineSourceTest, CandidatesComeBackInEngineOrder) {
  FakeTransport* t = nullptr;
  auto source = MakeSourceWith(&t, {"OK\tA\tB\tC"});
  const std::vector<Candidate> candidates = source->Query("s", 9);
  ASSERT_EQ(candidates.size(), 3u);
  EXPECT_EQ(candidates[0].text, L"A");
  EXPECT_EQ(candidates[1].text, L"B");
  EXPECT_EQ(candidates[2].text, L"C");
  EXPECT_GT(candidates[0].score, candidates[1].score);
  EXPECT_GT(candidates[1].score, candidates[2].score);
}

// ---- failure handling --------------------------------------------------

TEST(ExternalEngineSourceTest, EngineThatStopsAnsweringIsDroppedNotRetried) {
  FakeTransport* t = nullptr;
  auto source = MakeSourceWith(&t, {"OK\tA"});
  EXPECT_FALSE(source->Query("s", 9).empty());

  t->DieAfter(static_cast<int>(t->sent.size()));  // Next exchange fails.
  EXPECT_TRUE(source->Query("sa", 9).empty());
  EXPECT_FALSE(source->IsAvailable());

  // A dropped engine must not be poked again on the typing path.
  const size_t after_failure = t->sent.size();
  EXPECT_TRUE(source->Query("sai", 9).empty());
  EXPECT_EQ(t->sent.size(), after_failure);
}

TEST(ExternalEngineSourceTest, ErrResponseDropsTheEngine) {
  FakeTransport* t = nullptr;
  auto source = MakeSourceWith(&t, {"ERR\tunknown session"});
  EXPECT_TRUE(source->Query("s", 9).empty());
  EXPECT_FALSE(source->IsAvailable());
  EXPECT_NE(source->failure().find("unknown session"), std::string::npos);
}

TEST(ExternalEngineSourceTest, MalformedResponseDropsTheEngine) {
  FakeTransport* t = nullptr;
  auto source = MakeSourceWith(&t, {"gibberish"});
  EXPECT_TRUE(source->Query("s", 9).empty());
  EXPECT_FALSE(source->IsAvailable());
}

// A request carrying a control character would break the line framing, so it
// is never sent; the engine stays alive because this is not its fault.
TEST(ExternalEngineSourceTest, InputWithControlCharactersIsNotSent) {
  FakeTransport* t = nullptr;
  auto source = MakeSourceWith(&t, {"OK\tA"});
  t->sent.clear();
  EXPECT_TRUE(source->Query(std::string("sa\nEVIL"), 9).empty());
  EXPECT_TRUE(t->sent.empty());
  EXPECT_TRUE(source->IsAvailable());
}

TEST(ExternalEngineSourceTest, DisabledSourceAsksTheEngineNothing) {
  FakeTransport* t = nullptr;
  auto source = MakeSourceWith(&t, {"OK\tA"});
  source->set_enabled(false);
  t->sent.clear();
  EXPECT_TRUE(source->Query("s", 9).empty());
  EXPECT_TRUE(t->sent.empty());
  EXPECT_FALSE(source->IsAvailable());
}

// ---- candidate validation ----------------------------------------------

TEST(ExternalEngineSourceTest, CandidatesOutsideTheDeclaredRangeAreDropped) {
  InputProfile profile = EngineProfile();
  profile.engine_codepoints_set = true;
  profile.engine_codepoint_low = 0x1865;
  profile.engine_codepoint_high = 0x18AA;

  FakeTransport* t = nullptr;
  auto transport = std::make_unique<FakeTransport>();
  transport->replies = {Hello(),
                        // In range, then out of range, then in range again.
                        "OK\t\xE1\xA1\xA5\tZ\t\xE1\xA2\xAA"};
  t = transport.get();
  ExternalEngineSource source(profile, std::move(transport));
  const std::vector<Candidate> candidates = source.Query("s", 9);
  ASSERT_EQ(candidates.size(), 2u);
  EXPECT_EQ(candidates[0].text, std::wstring(1, static_cast<wchar_t>(0x1865)));
  EXPECT_EQ(candidates[1].text, std::wstring(1, static_cast<wchar_t>(0x18AA)));
  (void)t;
}

TEST(ExternalEngineSourceTest, SurplusCandidatesAreDiscarded) {
  FakeTransport* t = nullptr;
  std::string reply = "OK";
  for (int i = 0; i < 200; ++i) reply += "\tA";
  auto source = MakeSourceWith(&t, {reply});
  EXPECT_LE(source->Query("s", 5).size(), 5u);
}

TEST(ExternalEngineSourceTest, CandidateValidationRejectsHostileText) {
  InputProfile plain;  // No declared range: only the generic rules apply.
  EXPECT_TRUE(IsAcceptableCandidate(L"ok", plain));
  EXPECT_FALSE(IsAcceptableCandidate(L"", plain));
  // A bidirectional override can make committed text misrepresent itself.
  EXPECT_FALSE(IsAcceptableCandidate(std::wstring(1, 0x202E), plain));
  EXPECT_FALSE(IsAcceptableCandidate(std::wstring(1, 0x2066), plain));
  EXPECT_FALSE(IsAcceptableCandidate(std::wstring(1, 0x07), plain));
  EXPECT_FALSE(IsAcceptableCandidate(std::wstring(1, 0x7F), plain));
  EXPECT_FALSE(
      IsAcceptableCandidate(std::wstring(kEngineMaxCandidateChars + 1, L'a'),
                            plain));
  EXPECT_TRUE(
      IsAcceptableCandidate(std::wstring(kEngineMaxCandidateChars, L'a'),
                            plain));
}

TEST(ExternalEngineSourceTest, PriorityDefaultsBelowTheLexicon) {
  FakeTransport* t = nullptr;
  auto source = MakeSource(&t, EngineProfile());
  EXPECT_EQ(source->priority(), 90);

  InputProfile profile = EngineProfile();
  profile.engine_priority = 150;
  FakeTransport* other = nullptr;
  auto custom = MakeSource(&other, profile);
  EXPECT_EQ(custom->priority(), 150);
}

}  // namespace
}  // namespace paiza
