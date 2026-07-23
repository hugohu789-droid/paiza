// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid
//
// INI-backed user settings for the Linux (IBus) input method. The fields
// and their semantics mirror the Windows registry store and the macOS
// NSUserDefaults store 1:1 — each platform owns its persistence
// mechanism; any new field must be added to all three (feature-parity
// contract, see docs/linux-port.md).
//
// Pure standard C++ (std::filesystem + fstream), so this file also
// builds and unit-tests on Windows/macOS CI.

#ifndef PAIZA_LINUX_SETTINGS_STORE_H_
#define PAIZA_LINUX_SETTINGS_STORE_H_

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>

#include "core/localization.h"

namespace paiza::linux_ime {

// Default Mongolian font for the (future v2) candidate window.
inline constexpr wchar_t kDefaultCandidateFont[] = L"Noto Sans Mongolian";

struct Settings {
  uint32_t candidates_per_page = 7;  // 1..9 (digit keys select).
  bool cloud_source_enabled = false;
  // UI language; defaults to the OS display language (English when the
  // OS language is not among the supported set).
  UiLanguage ui_language = UiLanguage::kEnglish;
  // Font family for the Mongolian candidate text (v2 custom window).
  std::wstring candidate_font = kDefaultCandidateFont;
  // Soft-equivalence pairs (all on by default): a word spelled with one key
  // of the pair is also recalled (ranked below the exact spelling) when the
  // other is typed. Off = precise. Mirrors the Windows/macOS Settings 1:1
  // (see docs/platform-parity.md).
  bool equiv_ou = true;  // U+1825 / U+1826 (feminine oe / ue).
  bool equiv_qv = true;  // U+1823 / U+1824 (masculine o / u).
  bool equiv_hg = true;  // U+182C / U+182D (velar qa / ga).
  bool equiv_td = true;  // U+1832 / U+1833 (ta / da).
  bool equiv_ew = true;  // U+1827 / U+1838 (ee / wa, same glyph).
};

// Full path of the settings file: <UserConfigDirectory>/settings.ini.
std::wstring SettingsFilePath();

class SettingsStore {
 public:
  // Reads settings from `path` (SettingsFilePath() by default); missing
  // file or values fall back to defaults (ui_language additionally falls
  // back to DetectSystemLanguage()).
  static Settings Load();
  static Settings LoadFromFile(const std::wstring& path);

  // Writes the INI (creating the parent directory if needed). Returns
  // false when the file cannot be written.
  static bool Save(const Settings& settings);
  static bool SaveToFile(const Settings& settings, const std::wstring& path);
};

// Hot-reloading wrapper for the engine: Get() re-reads the file when its
// timestamp changed (checks throttled to every 500 ms), so a Save from
// the settings dialog applies on the next keystroke. Same pattern as
// KeyMappingStore. Not thread-safe; each consumer owns its watcher.
class SettingsWatcher {
 public:
  SettingsWatcher();                        // Uses SettingsFilePath().
  explicit SettingsWatcher(std::wstring path);  // Custom path (tests).

  const Settings& Get();

 private:
  std::wstring path_;
  Settings settings_;
  std::filesystem::file_time_type last_write_ =
      std::filesystem::file_time_type::min();
  std::chrono::steady_clock::time_point next_check_{};
};

}  // namespace paiza::linux_ime

#endif  // PAIZA_LINUX_SETTINGS_STORE_H_
