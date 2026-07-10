// Copyright 2026 Paiza. All rights reserved.

#include "core/key_mapping_store.h"

#include <fstream>
#include <sstream>

#include "common/logging.h"
#include "common/paths.h"

namespace paiza {
namespace {

constexpr std::chrono::milliseconds kCheckInterval{500};

// Last-write time, or file_time_type::min() when the file is inaccessible.
std::filesystem::file_time_type QueryLastWrite(const std::wstring& path) {
  std::error_code ec;
  const auto time = std::filesystem::last_write_time(path, ec);
  return ec ? std::filesystem::file_time_type::min() : time;
}

std::string ReadFileBytes(const std::wstring& path) {
  std::ifstream file(std::filesystem::path(path), std::ios::binary);
  if (!file.is_open()) return {};
  std::ostringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

bool WriteFileBytes(const std::wstring& path, const std::string& bytes) {
  std::ofstream file(std::filesystem::path(path),
                     std::ios::binary | std::ios::trunc);
  if (!file.is_open()) return false;
  file << bytes;
  return file.good();
}

}  // namespace

std::wstring KeyMappingFilePath() {
  const std::wstring dir = UserConfigDirectory();
  if (dir.empty()) return {};
  return (std::filesystem::path(dir) / L"keymap.txt").wstring();
}

KeyMappingStore::KeyMappingStore() : KeyMappingStore(KeyMappingFilePath()) {}

KeyMappingStore::KeyMappingStore(std::wstring path)
    : path_(std::move(path)) {
  ReloadIfChanged();
}

const KeyMapping& KeyMappingStore::Get() {
  const auto now = std::chrono::steady_clock::now();
  if (now >= next_check_) {
    next_check_ = now + kCheckInterval;
    ReloadIfChanged();
  }
  return mapping_;
}

void KeyMappingStore::ReloadNow() {
  next_check_ = std::chrono::steady_clock::now() + kCheckInterval;
  ReloadIfChanged();
}

void KeyMappingStore::ReloadIfChanged() {
  if (path_.empty()) return;
  const auto current = QueryLastWrite(path_);
  if (current == last_write_) return;
  last_write_ = current;

  const bool exists = current != std::filesystem::file_time_type::min();

  // Validate before applying: a botched edit must never silently drop
  // half the user's mapping. On failure keep the current mapping,
  // restore the last valid content over the broken file and report the
  // offending line through file_status().
  if (exists) {
    const std::optional<KeyMapFileError> error = ValidateKeyMapFile(path_);
    if (error.has_value()) {
      KeyMapFileStatus status;
      status.valid = false;
      status.error_line = error->line_number;
      status.error_text = error->line;
      status.restored =
          !last_good_content_.empty() &&
          WriteFileBytes(path_, last_good_content_);
      if (status.restored) {
        // Don't treat our own restore write as a fresh edit.
        last_write_ = QueryLastWrite(path_);
      }
      file_status_ = std::move(status);
      ++status_revision_;
      Logf(LogLevel::kWarning,
           L"Key mapping rejected: line %zu invalid%ls", error->line_number,
           file_status_.restored ? L", file restored" : L"");
      return;
    }
  }

  // Rebuild from defaults, then overlay the file (a deleted file simply
  // restores the defaults).
  KeyMapping fresh = KeyMapping::CreateDefault();
  if (exists) {
    fresh.LoadFromFile(path_);
    last_good_content_ = ReadFileBytes(path_);
  } else {
    last_good_content_.clear();
  }
  mapping_ = std::move(fresh);
  ++revision_;
  if (!file_status_.valid) {
    file_status_ = KeyMapFileStatus{};
    ++status_revision_;
  }
}

}  // namespace paiza
