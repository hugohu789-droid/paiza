// Copyright 2026 Paiza. All rights reserved.
//
// Hot-reloading provider for the user key mapping. Both the input-method
// module (per keystroke) and the settings dialog (preview) call Get(); the
// file's last-write time is polled with a small throttle, so edits to the
// config file take effect on the next keystroke without restarting
// anything. Platform-neutral (std::filesystem / std::chrono).

#ifndef PAIZA_CORE_KEY_MAPPING_STORE_H_
#define PAIZA_CORE_KEY_MAPPING_STORE_H_

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>

#include "core/key_mapping.h"

namespace paiza {

// Full path of the user mapping file: <UserConfigDirectory>/keymap.txt.
// Note: sandboxed hosts may not be able to read the user profile; the
// store silently keeps the built-in defaults there.
std::wstring KeyMappingFilePath();

class KeyMappingStore {
 public:
  // Uses KeyMappingFilePath().
  KeyMappingStore();
  // Custom path (tests).
  explicit KeyMappingStore(std::wstring path);

  // Returns the current mapping, reloading the file if it changed since
  // the last check (checks are throttled to every 500 ms). Not
  // thread-safe; each consumer owns its store.
  const KeyMapping& Get();

  // Increments every time a file change is picked up; lets UI consumers
  // cheaply detect "something changed" without comparing mappings.
  uint64_t revision() const { return revision_; }

  const std::wstring& path() const { return path_; }

 private:
  void ReloadIfChanged();

  std::wstring path_;
  KeyMapping mapping_ = KeyMapping::CreateDefault();
  std::filesystem::file_time_type last_write_ =
      std::filesystem::file_time_type::min();
  std::chrono::steady_clock::time_point next_check_{};
  uint64_t revision_ = 1;
};

}  // namespace paiza

#endif  // PAIZA_CORE_KEY_MAPPING_STORE_H_
