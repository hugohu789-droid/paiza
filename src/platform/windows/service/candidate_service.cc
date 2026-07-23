// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid

#include "service/candidate_service.h"

#include <algorithm>

#include "common/logging.h"

namespace paiza::service {

using ipc::Message;
using ipc::MessageType;

CandidateService::CandidateService(std::shared_ptr<CandidateEngine> engine,
                                   UserDictionary* user_dictionary,
                                   StaticLexicon* static_lexicon,
                                   std::function<void()> show_settings)
    : engine_(std::move(engine)),
      user_dictionary_(user_dictionary),
      static_lexicon_(static_lexicon),
      show_settings_(std::move(show_settings)) {}

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

    case MessageType::kCommitCandidate: {
      const auto notice = ipc::DecodeCommitNotice(request);
      if (notice.has_value()) {
        if (user_dictionary_ != nullptr) {
          user_dictionary_->RecordCommit(notice->latin_input, notice->word);
        }
        // Set the bigram context so the next query favours likely successors.
        if (static_lexicon_ != nullptr) {
          static_lexicon_->SetContextWord(
              static_lexicon_->WordIdOf(notice->latin_input, notice->word));
        }
      }
      return {MessageType::kAck, {}};
    }

    default:
      PAIZA_LOG_WARN(L"Unknown IPC message type");
      return {MessageType::kAck, {}};
  }
}

}  // namespace paiza::service
