// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid

#include "ipc/message.h"

#include <cstring>

#include "common/string_utils.h"

namespace paiza::ipc {
namespace {

// Append-only little-endian byte writer.
class ByteWriter {
 public:
  void WriteU32(uint32_t value) {
    buffer_.push_back(static_cast<uint8_t>(value & 0xFF));
    buffer_.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    buffer_.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    buffer_.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
  }

  void WriteI32(int32_t value) { WriteU32(static_cast<uint32_t>(value)); }

  void WriteString(const std::string& utf8) {
    WriteU32(static_cast<uint32_t>(utf8.size()));
    buffer_.insert(buffer_.end(), utf8.begin(), utf8.end());
  }

  void WriteWString(const std::wstring& wide) { WriteString(WideToUtf8(wide)); }

  std::vector<uint8_t> Take() { return std::move(buffer_); }

 private:
  std::vector<uint8_t> buffer_;
};

// Bounds-checked little-endian byte reader. Any overrun poisons the reader.
class ByteReader {
 public:
  ByteReader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

  bool ok() const { return ok_; }

  uint32_t ReadU32() {
    if (!Require(4)) return 0;
    uint32_t value = static_cast<uint32_t>(data_[pos_]) |
                     (static_cast<uint32_t>(data_[pos_ + 1]) << 8) |
                     (static_cast<uint32_t>(data_[pos_ + 2]) << 16) |
                     (static_cast<uint32_t>(data_[pos_ + 3]) << 24);
    pos_ += 4;
    return value;
  }

  int32_t ReadI32() { return static_cast<int32_t>(ReadU32()); }

  std::string ReadString() {
    const uint32_t length = ReadU32();
    if (!Require(length)) return {};
    std::string value(reinterpret_cast<const char*>(data_ + pos_), length);
    pos_ += length;
    return value;
  }

  std::wstring ReadWString() { return Utf8ToWide(ReadString()); }

 private:
  bool Require(size_t bytes) {
    if (!ok_ || size_ - pos_ < bytes) {
      ok_ = false;
      return false;
    }
    return true;
  }

  const uint8_t* data_;
  size_t size_;
  size_t pos_ = 0;
  bool ok_ = true;
};

}  // namespace

std::vector<uint8_t> EncodeFrame(const Message& message) {
  MessageHeader header;
  header.type = static_cast<uint32_t>(message.type);
  header.payload_size = static_cast<uint32_t>(message.payload.size());

  std::vector<uint8_t> frame(sizeof(MessageHeader) + message.payload.size());
  std::memcpy(frame.data(), &header, sizeof(header));
  if (!message.payload.empty()) {
    std::memcpy(frame.data() + sizeof(header), message.payload.data(),
                message.payload.size());
  }
  return frame;
}

std::optional<Message> DecodeFrame(const uint8_t* data, size_t size) {
  if (data == nullptr || size < sizeof(MessageHeader)) return std::nullopt;
  MessageHeader header;
  std::memcpy(&header, data, sizeof(header));
  if (header.magic != kProtocolMagic) return std::nullopt;
  if (header.version != kProtocolVersion) return std::nullopt;
  if (header.payload_size > kMaxPayloadBytes) return std::nullopt;
  if (size != sizeof(header) + header.payload_size) return std::nullopt;

  Message message;
  message.type = static_cast<MessageType>(header.type);
  message.payload.assign(data + sizeof(header), data + size);
  return message;
}

Message EncodeQueryRequest(const QueryRequest& request) {
  ByteWriter writer;
  writer.WriteString(request.latin_input);
  writer.WriteU32(request.max_results);
  return {MessageType::kQueryCandidates, writer.Take()};
}

std::optional<QueryRequest> DecodeQueryRequest(const Message& message) {
  if (message.type != MessageType::kQueryCandidates) return std::nullopt;
  ByteReader reader(message.payload.data(), message.payload.size());
  QueryRequest request;
  request.latin_input = reader.ReadString();
  request.max_results = reader.ReadU32();
  if (!reader.ok()) return std::nullopt;
  return request;
}

Message EncodeQueryResponse(const QueryResponse& response) {
  ByteWriter writer;
  writer.WriteU32(static_cast<uint32_t>(response.candidates.size()));
  for (const Candidate& candidate : response.candidates) {
    writer.WriteWString(candidate.text);
    writer.WriteWString(candidate.comment);
    writer.WriteI32(candidate.score);
  }
  return {MessageType::kQueryCandidatesReply, writer.Take()};
}

std::optional<QueryResponse> DecodeQueryResponse(const Message& message) {
  if (message.type != MessageType::kQueryCandidatesReply) return std::nullopt;
  ByteReader reader(message.payload.data(), message.payload.size());
  const uint32_t count = reader.ReadU32();
  QueryResponse response;
  for (uint32_t i = 0; i < count && reader.ok(); ++i) {
    Candidate candidate;
    candidate.text = reader.ReadWString();
    candidate.comment = reader.ReadWString();
    candidate.score = reader.ReadI32();
    response.candidates.push_back(std::move(candidate));
  }
  if (!reader.ok()) return std::nullopt;
  return response;
}

Message EncodeCommitNotice(const CommitNotice& notice) {
  ByteWriter writer;
  writer.WriteString(notice.latin_input);
  writer.WriteWString(notice.word);
  return {MessageType::kCommitCandidate, writer.Take()};
}

std::optional<CommitNotice> DecodeCommitNotice(const Message& message) {
  if (message.type != MessageType::kCommitCandidate) return std::nullopt;
  ByteReader reader(message.payload.data(), message.payload.size());
  CommitNotice notice;
  notice.latin_input = reader.ReadString();
  notice.word = reader.ReadWString();
  if (!reader.ok()) return std::nullopt;
  return notice;
}

}  // namespace paiza::ipc
