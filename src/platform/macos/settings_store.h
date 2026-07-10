// Copyright 2026 Paiza. All rights reserved.
//
// NSUserDefaults-backed user settings for the macOS input method. The
// fields and their semantics mirror the Windows registry store
// (src/platform/windows/service/settings_store.h) 1:1 — the two structs
// are kept separate only because each platform owns its persistence
// mechanism; any new field must be added to both (feature-parity
// contract, see docs/macos-port.md).

#ifndef PAIZA_MACOS_SETTINGS_STORE_H_
#define PAIZA_MACOS_SETTINGS_STORE_H_

#include <cstdint>
#include <string>

#include "core/localization.h"

namespace paiza::mac {

// Default Mongolian font for the candidate window (spec: Google's Noto;
// macOS ships it in /System/Library/Fonts/Supplemental).
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

}  // namespace paiza::mac

#endif  // PAIZA_MACOS_SETTINGS_STORE_H_
