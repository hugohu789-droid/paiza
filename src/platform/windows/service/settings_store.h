// Copyright 2026 Paiza. All rights reserved.
//
// Registry-backed user settings (HKCU\Software\PaizaIME). The service
// process is the single owner of settings; the TSF module never touches
// them directly.

#ifndef PAIZA_SERVICE_SETTINGS_STORE_H_
#define PAIZA_SERVICE_SETTINGS_STORE_H_

#include <cstdint>
#include <string>

#include "core/localization.h"

namespace paiza::service {

// Default Mongolian font for the candidate window (spec: Google's Noto).
inline constexpr wchar_t kDefaultCandidateFont[] = L"Noto Sans Mongolian";

struct Settings {
  uint32_t candidates_per_page = 7;  // 1..9 (digit keys select).
  bool cloud_source_enabled = false;
  // UI language; defaults to the OS display language (English when the OS
  // language is not among the supported set).
  UiLanguage ui_language = UiLanguage::kEnglish;
  // Font family used for the Mongolian text in the candidate window.
  std::wstring candidate_font = kDefaultCandidateFont;
};

class SettingsStore {
 public:
  // Reads settings; missing values fall back to defaults.
  static Settings Load();
  static void Save(const Settings& settings);
};

}  // namespace paiza::service

#endif  // PAIZA_SERVICE_SETTINGS_STORE_H_
