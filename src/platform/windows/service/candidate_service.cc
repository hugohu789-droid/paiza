// Copyright 2026 Paiza. All rights reserved.

#include "service/candidate_service.h"

#include <algorithm>

#include "common/logging.h"

namespace paiza::service {

using ipc::Message;
using ipc::MessageType;

CandidateService::CandidateService(std::shared_ptr<CandidateEngine> engine,
                                   std::function<void()> show_settings)
    : engine_(std::move(engine)), show_settings_(std::move(show_settings)) {}

Message CandidateService::HandleMessage(const Message& request) {
  switch (request.type) {
    case MessageType::kPing:
      return {MessageType::kPingReply, {}};

    case MessageType::kQueryCandidates: {
      const auto query = ipc::DecodeQueryRequest(request);
      ipc::QueryResponse response;
      if (query.has_value()) {
        const uint32_t max_results =
            std::clamp<uint32_t>(query->max_results, 1, 100);
        response.candidates = engine_->Query(query->latin_input, max_results);
      }
      return ipc::EncodeQueryResponse(response);
    }

    case MessageType::kShowSettings:
      if (show_settings_) show_settings_();
      return {MessageType::kAck, {}};

    default:
      PAIZA_LOG_WARN(L"Unknown IPC message type");
      return {MessageType::kAck, {}};
  }
}

}  // namespace paiza::service
