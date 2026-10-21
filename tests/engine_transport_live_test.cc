// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid
//
// The transports, against a real process.
//
// Everything else about the engine path is covered by unit tests with a fake
// transport. What those cannot reach is the I/O itself: the overlapped read
// and CancelIoEx on Windows, the poll() deadline on POSIX, EOF when a child
// exits, and an oversized line arriving in pieces. Those only happen with a
// process on the other end, so these tests spawn paiza_test_engine, which
// can be asked to misbehave on command.
//
// Each platform runs its own transport here, so CI covers all three even
// though only one compiles on any one machine.

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "core/engine_transport.h"
#include "core/external_engine_source.h"
#include "core/input_profile.h"

namespace paiza {
namespace {

// Where CMake put the stub engine, passed in as a define so the test does not
// have to guess at the build layout.
std::wstring EnginePath() { return std::wstring(PAIZA_TEST_ENGINE); }

std::unique_ptr<EngineTransport> Spawn(const std::wstring& args) {
  std::string reason;
  auto transport = SpawnEngineTransport(EnginePath(), args, &reason);
  EXPECT_NE(transport, nullptr) << reason;
  return transport;
}

// A profile that accepts the stub's ASCII output. No code-point range: the
// range is covered by the unit tests, and constraining it here would only
// obscure which layer rejected something.
InputProfile StubProfile() {
  InputProfile profile;
  profile.id = "stub";
  profile.engine = L"paiza_test_engine";
  profile.engine_protocol = "paiza-engine";
  return profile;
}

int64_t ElapsedMs(std::chrono::steady_clock::time_point since) {
  using namespace std::chrono;
  return duration_cast<milliseconds>(steady_clock::now() - since).count();
}

// --------------------------------------------------------- the happy path ---

TEST(EngineTransportLive, ExchangesLines) {
  auto transport = Spawn(L"");
  ASSERT_NE(transport, nullptr);
  std::string response;
  ASSERT_TRUE(transport->Exchange("HELLO", &response));
  EXPECT_EQ(response, "OK\tpaiza-engine\t1");
  EXPECT_TRUE(transport->alive());

  ASSERT_TRUE(transport->Exchange("TYPE 0 a fuzzy 8", &response));
  EXPECT_EQ(response, "OK\ta\tA");
  ASSERT_TRUE(transport->Exchange("TYPE 0 b fuzzy 8", &response));
  EXPECT_EQ(response, "OK\tab\tAB");
  ASSERT_TRUE(transport->Exchange("UNTYPE 0 fuzzy 8", &response));
  EXPECT_EQ(response, "OK\ta\tA");
  EXPECT_TRUE(transport->alive());
}

// Many exchanges in a row: this is where a transport that leaks a handle, an
// event or a buffered byte per round trip shows up.
TEST(EngineTransportLive, SurvivesManyExchanges) {
  auto transport = Spawn(L"");
  ASSERT_NE(transport, nullptr);
  std::string response;
  ASSERT_TRUE(transport->Exchange("HELLO", &response));
  for (int i = 0; i < 300; ++i) {
    ASSERT_TRUE(transport->Exchange("TYPE 0 a fuzzy 8", &response)) << i;
    ASSERT_TRUE(transport->Exchange("UNTYPE 0 fuzzy 8", &response)) << i;
  }
  EXPECT_TRUE(transport->alive());
}

TEST(EngineTransportLive, PassesArgumentsThrough) {
  // --garbage is only reachable if the argument actually arrived.
  auto transport = Spawn(L"--garbage");
  ASSERT_NE(transport, nullptr);
  std::string response;
  ASSERT_TRUE(transport->Exchange("HELLO", &response));
  ASSERT_TRUE(transport->Exchange("TYPE 0 a fuzzy 8", &response));
  EXPECT_EQ(response, "WAT\tthis is not a valid response");
}

// ------------------------------------------------------- the failure paths ---

// The one that matters most: a wedged engine must not hang the caller. On
// Windows this is the CancelIoEx path, on POSIX the poll() timeout.
TEST(EngineTransportLive, GivesUpOnAnEngineThatNeverAnswers) {
  auto transport = Spawn(L"--hang");
  ASSERT_NE(transport, nullptr);
  std::string response;
  ASSERT_TRUE(transport->Exchange("HELLO", &response));

  const auto started = std::chrono::steady_clock::now();
  EXPECT_FALSE(transport->Exchange("TYPE 0 a fuzzy 8", &response));
  const int64_t elapsed = ElapsedMs(started);
  EXPECT_GE(elapsed, kEngineTimeoutMs / 2) << "returned before timing out";
  // Generous upper bound: this runs on shared CI machines, and the point is
  // that the wait is bounded, not that it is precise.
  EXPECT_LT(elapsed, kEngineTimeoutMs * 10) << "waited far too long";

  // Dead is permanent. Retrying a wedged engine on the typing path is how an
  // IME hangs, so a failed transport must stay failed.
  EXPECT_FALSE(transport->alive());
  EXPECT_FALSE(transport->Exchange("HELLO", &response));
}

TEST(EngineTransportLive, DetectsAnEngineThatExits) {
  auto transport = Spawn(L"--die");
  ASSERT_NE(transport, nullptr);
  std::string response;
  ASSERT_TRUE(transport->Exchange("HELLO", &response));
  EXPECT_FALSE(transport->Exchange("TYPE 0 a fuzzy 8", &response));
  EXPECT_FALSE(transport->alive());
}

// A 200 KB line against a 64 KB cap, arriving in whatever chunks the pipe
// hands over. Without the cap this is a memory denial of service.
TEST(EngineTransportLive, RejectsAnOversizedResponse) {
  auto transport = Spawn(L"--flood");
  ASSERT_NE(transport, nullptr);
  std::string response;
  ASSERT_TRUE(transport->Exchange("HELLO", &response));
  EXPECT_FALSE(transport->Exchange("TYPE 0 a fuzzy 8", &response));
  EXPECT_FALSE(transport->alive());
}

// A slow *handshake* must fail construction, not leave a half-working source
// that fails later on a keystroke.
TEST(EngineTransportLive, ASlowHandshakeFailsAtStartup) {
  auto transport = Spawn(L"--slow-hello");
  ASSERT_NE(transport, nullptr);
  ExternalEngineSource source(StubProfile(), std::move(transport));
  EXPECT_FALSE(source.IsAvailable());
  EXPECT_FALSE(source.failure().empty());
}

// ------------------------------------- the whole stack over a real process ---

// ExternalEngineSource's unit tests use a fake transport, so this is the only
// place the source, the protocol, the framing and a real pipe are exercised
// together.
TEST(EngineTransportLive, SourceQueriesThroughARealProcess) {
  auto transport = Spawn(L"");
  ASSERT_NE(transport, nullptr);
  ExternalEngineSource source(StubProfile(), std::move(transport));
  ASSERT_TRUE(source.IsAvailable()) << source.failure();

  std::vector<Candidate> candidates = source.Query("ab", 8);
  ASSERT_EQ(candidates.size(), 2u);
  EXPECT_EQ(candidates[0].text, L"ab");
  EXPECT_EQ(candidates[1].text, L"AB");
  // Ordered by the engine, so the first must score highest.
  EXPECT_GT(candidates[0].score, candidates[1].score);

  // Appending reuses the session: one more key, not a re-send.
  candidates = source.Query("abc", 8);
  ASSERT_FALSE(candidates.empty());
  EXPECT_EQ(candidates[0].text, L"abc");

  // Deleting from the end walks back.
  candidates = source.Query("ab", 8);
  ASSERT_FALSE(candidates.empty());
  EXPECT_EQ(candidates[0].text, L"ab");

  // A divergent edit restarts the session rather than splicing nonsense.
  candidates = source.Query("xy", 8);
  ASSERT_FALSE(candidates.empty());
  EXPECT_EQ(candidates[0].text, L"xy");

  EXPECT_TRUE(source.IsAvailable());
}

// A source whose engine wedges mid-word must simply stop contributing, and
// the caller must still get an answer promptly.
TEST(EngineTransportLive, SourceStopsContributingWhenTheEngineWedges) {
  auto transport = Spawn(L"--hang");
  ASSERT_NE(transport, nullptr);
  ExternalEngineSource source(StubProfile(), std::move(transport));
  ASSERT_TRUE(source.IsAvailable()) << source.failure();

  EXPECT_TRUE(source.Query("a", 8).empty());
  EXPECT_FALSE(source.IsAvailable());
  EXPECT_FALSE(source.failure().empty());

  // And no further round trips are attempted, so later keystrokes are free.
  const auto started = std::chrono::steady_clock::now();
  for (int i = 0; i < 20; ++i) EXPECT_TRUE(source.Query("abc", 8).empty());
  EXPECT_LT(ElapsedMs(started), kEngineTimeoutMs)
      << "kept talking to a dead engine";
}

}  // namespace
}  // namespace paiza
