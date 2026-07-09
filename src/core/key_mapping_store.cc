// Copyright 2026 Paiza. All rights reserved.

#include "core/key_mapping_store.h"

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

void KeyMappingStore::ReloadIfChanged() {
  if (path_.empty()) return;
  const auto current = QueryLastWrite(path_);
  if (current == last_write_) return;
  last_write_ = current;

  // Rebuild from defaults, then overlay the file (a deleted file simply
  // restores the defaults).
  KeyMapping fresh = KeyMapping::CreateDefault();
  if (current != std::filesystem::file_time_type::min()) {
    fresh.LoadFromFile(path_);
  }
  mapping_ = std::move(fresh);
  ++revision_;
}

}  // namespace paiza
