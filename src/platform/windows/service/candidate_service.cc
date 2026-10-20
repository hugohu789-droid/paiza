// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid

#include "service/candidate_service.h"

#include <algorithm>

#include "common/logging.h"

namespace paiza::service {

using ipc::Message;
using ipc::MessageType;

CandidateService::CandidateService(std::shared_ptr<CandidateEngine> engine,
                                   ProfileSwitch* profiles,
                                   std::function<void()> show_settings)
    : engine_(std::move(engine)),
      profiles_(profiles),
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
        UserDictionary* user_dictionary =
            profiles_ != nullptr ? profiles_->active_user_dictionary()
                                 : nullptr;
        if (user_dictionary != nullptr) {
          user_dictionary->RecordCommit(notice->latin_input, notice->word);
        }
        // Set the bigram context so the next query favours likely successors.
        StaticLexicon* static_lexicon =
            profiles_ != nullptr ? profiles_->active_static_lexicon()
                                 : nullptr;
        if (static_lexicon != nullptr) {
          static_lexicon->SetContextWord(
              static_lexicon->WordIdOf(notice->latin_input, notice->word));
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
