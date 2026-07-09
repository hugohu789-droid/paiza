// Copyright 2026 Paiza. All rights reserved.

#include "ipc/message.h"

#include <gtest/gtest.h>

namespace paiza::ipc {
namespace {

TEST(MessageTest, QueryRequestRoundTrip) {
  QueryRequest request;
  request.latin_input = "mongol";
  request.max_results = 27;

  const Message encoded = EncodeQueryRequest(request);
  EXPECT_EQ(encoded.type, MessageType::kQueryCandidates);

  const auto decoded = DecodeQueryRequest(encoded);
  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(decoded->latin_input, "mongol");
  EXPECT_EQ(decoded->max_results, 27u);
}

TEST(MessageTest, QueryResponseRoundTrip) {
  QueryResponse response;
  Candidate a;
  a.text = L"\x182E\x1823\x1829\x182D\x1823\x182F";
  a.comment = L"local";
  a.score = 42;
  response.candidates.push_back(a);
  Candidate b;
  b.text = L"\x1830\x1820";
  response.candidates.push_back(b);

  const Message encoded = EncodeQueryResponse(response);
  const auto decoded = DecodeQueryResponse(encoded);
  ASSERT_TRUE(decoded.has_value());
  ASSERT_EQ(decoded->candidates.size(), 2u);
  EXPECT_EQ(decoded->candidates[0].text, a.text);
  EXPECT_EQ(decoded->candidates[0].comment, L"local");
  EXPECT_EQ(decoded->candidates[0].score, 42);
  EXPECT_EQ(decoded->candidates[1].text, b.text);
}

TEST(MessageTest, FrameRoundTrip) {
  Message message{MessageType::kPing, {1, 2, 3}};
  const auto frame = EncodeFrame(message);
  const auto decoded = DecodeFrame(frame.data(), frame.size());
  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(decoded->type, MessageType::kPing);
  EXPECT_EQ(decoded->payload, (std::vector<uint8_t>{1, 2, 3}));
}

TEST(MessageTest, RejectsBadMagic) {
  Message message{MessageType::kPing, {}};
  auto frame = EncodeFrame(message);
  frame[0] ^= 0xFF;
  EXPECT_FALSE(DecodeFrame(frame.data(), frame.size()).has_value());
}

TEST(MessageTest, RejectsTruncatedFrame) {
  Message message{MessageType::kQueryCandidates, {1, 2, 3, 4}};
  const auto frame = EncodeFrame(message);
  EXPECT_FALSE(DecodeFrame(frame.data(), frame.size() - 1).has_value());
  EXPECT_FALSE(DecodeFrame(frame.data(), 3).has_value());
  EXPECT_FALSE(DecodeFrame(nullptr, 0).has_value());
}

TEST(MessageTest, RejectsMalformedPayload) {
  // A query-candidates message whose payload is too short to hold the
  // declared string: decoder must fail, not read out of bounds.
  Message message{MessageType::kQueryCandidates, {0xFF, 0xFF, 0xFF, 0x7F}};
  EXPECT_FALSE(DecodeQueryRequest(message).has_value());
}

TEST(MessageTest, RejectsMalformedResponsePayload) {
  // Claims 1000 candidates but carries no data.
  Message message{MessageType::kQueryCandidatesReply,
                  {0xE8, 0x03, 0x00, 0x00}};
  EXPECT_FALSE(DecodeQueryResponse(message).has_value());
}

}  // namespace
}  // namespace paiza::ipc
