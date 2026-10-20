// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid
//
// Registry-backed user settings (HKCU\Software\PaizaIME). The service
// process is the single owner of settings; the TSF module never touches
// them directly.

#ifndef PAIZA_SERVICE_SETTINGS_STORE_H_
#define PAIZA_SERVICE_SETTINGS_STORE_H_

#include <cstdint>
#include <string>

#include "core/input_profile.h"
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
  // Soft-equivalence pairs: when on, a word spelled with one key of the pair
  // is also recalled (ranked below the exact spelling) when the other is
  // typed. Off = precise (that pair is distinguished). All on by default.
  bool equiv_ou = true;  // U+1825 / U+1826 (feminine oe / ue).
  bool equiv_qv = true;  // U+1823 / U+1824 (masculine o / u).
  bool equiv_hg = true;  // U+182C / U+182D (velar qa / ga).
  bool equiv_td = true;  // U+1832 / U+1833 (ta / da).
  bool equiv_ew = true;  // U+1827 / U+1838 (ee / wa, same glyph).

  // Active input profile id (data/profiles/<id>.txt). Selects the text
  // encoding and its candidate engine; "unicode" is today's behaviour.
  // See docs/input-profiles.md.
  std::string active_profile = kDefaultProfileId;
};

class SettingsStore {
 public:
  // Reads settings; missing values fall back to defaults.
  static Settings Load();
  static void Save(const Settings& settings);
};

}  // namespace paiza::service

#endif  // PAIZA_SERVICE_SETTINGS_STORE_H_
