// Copyright 2026 Paiza. All rights reserved.
//
// End-to-end IPC test: real NamedPipeServer + NamedPipeClient exchanging
// frames over \\.\pipe\PaizaMongolianIme within one process.

#include <gtest/gtest.h>

#include "ipc/named_pipe_client.h"
#include "ipc/named_pipe_server.h"

namespace paiza::ipc {
namespace {

class IpcRoundTripTest : public ::testing::Test {
 protected:
  void SetUp() override {
    server_ = std::make_unique<NamedPipeServer>(
        [](const Message& request) -> Message {
          if (request.type == MessageType::kPing) {
            return {MessageType::kPingReply, {}};
          }
          if (request.type == MessageType::kQueryCandidates) {
            const auto query = DecodeQueryRequest(request);
            QueryResponse response;
            if (query.has_value()) {
              Candidate echo;
              echo.text = std::wstring(query->latin_input.begin(),
                                       query->latin_input.end());
              echo.score = static_cast<int32_t>(query->max_results);
              response.candidates.push_back(std::move(echo));
            }
            return EncodeQueryResponse(response);
          }
          return {MessageType::kAck, {}};
        });
    ASSERT_TRUE(server_->Start());
  }

  void TearDown() override { server_->Stop(); }

  std::unique_ptr<NamedPipeServer> server_;
  NamedPipeClient client_{NamedPipeClient::Options{
      .connect_timeout_ms = 2000, .io_timeout_ms = 2000}};
};

TEST_F(IpcRoundTripTest, Ping) {
  const auto reply = client_.Transact({MessageType::kPing, {}});
  ASSERT_TRUE(reply.has_value());
  EXPECT_EQ(reply->type, MessageType::kPingReply);
}

TEST_F(IpcRoundTripTest, QueryEchoesThroughPipe) {
  QueryRequest request;
  request.latin_input = "sain";
  request.max_results = 9;

  const auto reply = client_.Transact(EncodeQueryRequest(request));
  ASSERT_TRUE(reply.has_value());
  const auto response = DecodeQueryResponse(*reply);
  ASSERT_TRUE(response.has_value());
  ASSERT_EQ(response->candidates.size(), 1u);
  EXPECT_EQ(response->candidates[0].text, L"sain");
  EXPECT_EQ(response->candidates[0].score, 9);
}

TEST_F(IpcRoundTripTest, SequentialTransactions) {
  for (int i = 0; i < 5; ++i) {
    const auto reply = client_.Transact({MessageType::kPing, {}});
    ASSERT_TRUE(reply.has_value()) << "iteration " << i;
    EXPECT_EQ(reply->type, MessageType::kPingReply);
  }
}

}  // namespace
}  // namespace paiza::ipc
