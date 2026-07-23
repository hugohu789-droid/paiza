// Copyright 2026 Paiza. All rights reserved.
//
// Hot-reloading provider for the user key mapping. Both the input-method
// module (per keystroke) and the settings dialog (preview) call Get(); the
// file is polled with a small throttle, so edits to the config file take
// effect on the next keystroke without restarting anything. Change
// detection compares the file CONTENT, not the mtime: successive writes
// within the filesystem timestamp granularity (a real path here, because
// the store itself writes the file when restoring after a rejected edit)
// would be missed by an mtime check. keymap.txt is tiny, so reading it on
// the throttled poll is negligible. Platform-neutral.

#ifndef PAIZA_CORE_KEY_MAPPING_STORE_H_
#define PAIZA_CORE_KEY_MAPPING_STORE_H_

#include <chrono>
#include <cstdint>
#include <string>

#include "core/key_mapping.h"

namespace paiza {

// Full path of the user mapping file: <UserConfigDirectory>/keymap.txt.
// Note: sandboxed hosts may not be able to read the user profile; the
// store silently keeps the built-in defaults there.
std::wstring KeyMappingFilePath();

// Outcome of the store's last look at the mapping file. When the user
// saves a file that fails ValidateKeyMapFile, the store keeps the
// previous mapping, writes the last valid content back over the broken
// file (spec: on validation failure, show an error and restore the
// pre-edit version) and reports the error
// here so the settings dialogs can show it.
struct KeyMapFileStatus {
  bool valid = true;
  size_t error_line = 0;    // 1-based line of the first bad entry.
  std::string error_text;   // That line's raw text.
  bool restored = false;    // Previous valid content was written back.
};

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

  // Bypasses the throttle (settings dialogs on show; tests).
  void ReloadNow();

  // Increments every time a file change is picked up; lets UI consumers
  // cheaply detect "something changed" without comparing mappings.
  uint64_t revision() const { return revision_; }

  // Validation outcome of the most recent (re)load; status_revision()
  // increments whenever it changes, so UIs can refresh an error label
  // cheaply. Call Get() (or ReloadNow()) first to pick up fresh edits.
  const KeyMapFileStatus& file_status() const { return file_status_; }
  uint64_t status_revision() const { return status_revision_; }

  const std::wstring& path() const { return path_; }

 private:
  void ReloadIfChanged();

  std::wstring path_;
  KeyMapping mapping_ = KeyMapping::CreateDefault();
  // Raw bytes of the last file content that passed validation; written
  // back when a later edit fails validation. Empty = no valid file seen
  // (missing file, or invalid since the store started).
  std::string last_good_content_;
  KeyMapFileStatus file_status_;
  // Snapshot of the file as of the last poll (existence + raw bytes);
  // the change detector compares against this.
  bool last_exists_ = false;
  std::string last_content_;
  std::chrono::steady_clock::time_point next_check_{};
  uint64_t revision_ = 1;
  uint64_t status_revision_ = 1;
};

}  // namespace paiza

#endif  // PAIZA_CORE_KEY_MAPPING_STORE_H_
