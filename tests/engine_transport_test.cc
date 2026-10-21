// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid
//
// The transports themselves are platform code and only one of them compiles
// on any given machine, so the part that is easy to get wrong and identical
// in both -- turning a byte stream into lines, with a cap -- lives in core
// and is tested here, on every platform.

#include "core/engine_transport.h"

#include <gtest/gtest.h>

#include <string>

namespace paiza {
namespace {

TEST(TakeLine, TakesOneLineAndKeepsTheRest) {
  std::string buffer = "OK\tfirst\nOK\tsecond\n";
  std::string line;
  ASSERT_EQ(TakeLine(&buffer, &line), LineStatus::kComplete);
  EXPECT_EQ(line, "OK\tfirst");
  EXPECT_EQ(buffer, "OK\tsecond\n");
  ASSERT_EQ(TakeLine(&buffer, &line), LineStatus::kComplete);
  EXPECT_EQ(line, "OK\tsecond");
  EXPECT_TRUE(buffer.empty());
  EXPECT_EQ(TakeLine(&buffer, &line), LineStatus::kIncomplete);
}

// A pipe hands over whatever happens to have arrived, so a response can be
// split anywhere, including between the CR and the LF.
TEST(TakeLine, WaitsForTheTerminator) {
  std::string buffer = "OK\tpartial";
  std::string line = "untouched";
  EXPECT_EQ(TakeLine(&buffer, &line), LineStatus::kIncomplete);
  EXPECT_EQ(line, "untouched");
  EXPECT_EQ(buffer, "OK\tpartial");
  buffer += "\r";
  EXPECT_EQ(TakeLine(&buffer, &line), LineStatus::kIncomplete);
  buffer += "\n";
  ASSERT_EQ(TakeLine(&buffer, &line), LineStatus::kComplete);
  EXPECT_EQ(line, "OK\tpartial");
}

TEST(TakeLine, ToleratesCrlf) {
  std::string buffer = "OK\r\n";
  std::string line;
  ASSERT_EQ(TakeLine(&buffer, &line), LineStatus::kComplete);
  EXPECT_EQ(line, "OK");
  EXPECT_TRUE(buffer.empty());
}

TEST(TakeLine, AnEmptyLineIsALine) {
  std::string buffer = "\nOK\n";
  std::string line = "untouched";
  ASSERT_EQ(TakeLine(&buffer, &line), LineStatus::kComplete);
  EXPECT_TRUE(line.empty());
  EXPECT_EQ(buffer, "OK\n");
}

TEST(TakeLine, ALoneCrIsNotATerminator) {
  std::string buffer = "OK\rmore";
  std::string line;
  EXPECT_EQ(TakeLine(&buffer, &line), LineStatus::kIncomplete);
}

// A TAB-separated payload is the normal case; the framing must not touch it.
TEST(TakeLine, LeavesFieldsAlone) {
  std::string buffer = "OK\t\t a \tb\n";
  std::string line;
  ASSERT_EQ(TakeLine(&buffer, &line), LineStatus::kComplete);
  EXPECT_EQ(line, "OK\t\t a \tb");
}

// Without a cap, an engine that never sends a newline is a memory denial of
// service: the host would buffer forever waiting for a line that never ends.
TEST(TakeLine, OverflowsWhenNoTerminatorArrivesInTime) {
  std::string buffer(kEngineMaxResponseBytes - 1, 'x');
  std::string line;
  EXPECT_EQ(TakeLine(&buffer, &line), LineStatus::kIncomplete);
  buffer.push_back('x');
  EXPECT_EQ(TakeLine(&buffer, &line), LineStatus::kOverflow);
}

// Reading more would not help once the cap is passed, so the verdict must not
// change just because a terminator eventually turns up far beyond it.
TEST(TakeLine, OverflowsOnATerminatorBeyondTheCap) {
  std::string buffer(kEngineMaxResponseBytes + 1, 'x');
  buffer.push_back('\n');
  std::string line;
  EXPECT_EQ(TakeLine(&buffer, &line), LineStatus::kOverflow);
}

// Right at the cap is still a legal response: the limit is on the line, and
// a short line followed by a lot of junk must not be rejected either.
TEST(TakeLine, AcceptsALineAtTheCap) {
  std::string buffer(kEngineMaxResponseBytes, 'x');
  buffer.push_back('\n');
  std::string line;
  ASSERT_EQ(TakeLine(&buffer, &line), LineStatus::kComplete);
  EXPECT_EQ(line.size(), kEngineMaxResponseBytes);
}

TEST(TakeLine, AShortLineIsTakenEvenWhenMuchFollows) {
  std::string buffer = "OK\n" + std::string(kEngineMaxResponseBytes + 10, 'x');
  std::string line;
  ASSERT_EQ(TakeLine(&buffer, &line), LineStatus::kComplete);
  EXPECT_EQ(line, "OK");
}

TEST(SpawnEngineTransport, RefusesAnEmptyPath) {
  std::string reason;
  // An empty path must never reach the platform spawn code.
  EXPECT_EQ(SpawnEngineTransport(L"", L"", &reason), nullptr);
  EXPECT_FALSE(reason.empty());
}

// The one behaviour of the real transports that can be checked anywhere: a
// name that does not exist fails to spawn, with an explanation, rather than
// returning a transport that is dead on arrival.
TEST(SpawnEngineTransport, FailsWithAReasonWhenTheFileIsMissing) {
  std::string reason;
  const auto transport = SpawnEngineTransport(
      L"C:\\paiza-does-not-exist\\no-such-engine", L"", &reason);
  EXPECT_EQ(transport, nullptr);
  EXPECT_FALSE(reason.empty());
}

}  // namespace
}  // namespace paiza
