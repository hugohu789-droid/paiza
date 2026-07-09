// Copyright 2026 Paiza. All rights reserved.
//
// Wire protocol between the TSF text service (client) and the candidate
// service process (server). Deliberately tiny and dependency-free:
//
//   [MessageHeader][payload bytes]
//
// All integers are little-endian uint32. Strings are length-prefixed UTF-8.
// Decoding is fully bounds-checked: a malformed buffer never reads out of
// range (the pipe is reachable from low-integrity processes, so the server
// must treat input as untrusted).

#ifndef PAIZA_IPC_MESSAGE_H_
#define PAIZA_IPC_MESSAGE_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/candidate.h"

namespace paiza::ipc {

inline constexpr uint32_t kProtocolMagic = 0x505A4D49;  // "PZMI"
inline constexpr uint32_t kProtocolVersion = 1;
inline constexpr uint32_t kMaxPayloadBytes = 1 << 20;  // 1 MiB sanity cap.

enum class MessageType : uint32_t {
  kPing = 1,
  kPingReply = 2,
  kQueryCandidates = 3,
  kQueryCandidatesReply = 4,
  kShowSettings = 5,
  kAck = 6,
};

struct MessageHeader {
  uint32_t magic = kProtocolMagic;
  uint32_t version = kProtocolVersion;
  uint32_t type = 0;
  uint32_t payload_size = 0;
};
static_assert(sizeof(MessageHeader) == 16);

struct Message {
  MessageType type = MessageType::kPing;
  std::vector<uint8_t> payload;
};

// ------------------------------------------------------------------ frames --

// Serializes header + payload into one buffer ready for WriteFile.
std::vector<uint8_t> EncodeFrame(const Message& message);

// Parses a complete frame received from the pipe. Returns nullopt on any
// malformed input (bad magic/version, size mismatch, oversized payload).
std::optional<Message> DecodeFrame(const uint8_t* data, size_t size);

// ---------------------------------------------------------------- payloads --

struct QueryRequest {
  std::string latin_input;
  uint32_t max_results = 9;
};

struct QueryResponse {
  std::vector<Candidate> candidates;
};

Message EncodeQueryRequest(const QueryRequest& request);
std::optional<QueryRequest> DecodeQueryRequest(const Message& message);

Message EncodeQueryResponse(const QueryResponse& response);
std::optional<QueryResponse> DecodeQueryResponse(const Message& message);

}  // namespace paiza::ipc

#endif  // PAIZA_IPC_MESSAGE_H_
