// Copyright 2026 Paiza. All rights reserved.
//
// UI localization shared by every platform front-end (settings dialogs).
// Three languages are supported: English, Simplified Chinese and Mongolian
// Cyrillic. The default follows the OS display language; anything outside
// the supported set falls back to English. The user can override the
// language in the settings dialog (persisted by the platform's settings
// store).

#ifndef PAIZA_CORE_LOCALIZATION_H_
#define PAIZA_CORE_LOCALIZATION_H_

#include <cstdint>

namespace paiza {

enum class UiLanguage : uint32_t {
  kEnglish = 0,
  kChinese = 1,
  kCyrillic = 2,
};

inline constexpr uint32_t kUiLanguageCount = 3;

// All user-visible strings of the settings dialog.
struct UiStrings {
  const wchar_t* window_title;
  const wchar_t* app_title;  // Brand name; identical in every language.
  const wchar_t* subtitle;
  const wchar_t* section_general;
  const wchar_t* candidates_per_page;
  const wchar_t* cloud_candidates;
  const wchar_t* toggle_on;
  const wchar_t* toggle_off;
  const wchar_t* language_label;
  const wchar_t* candidate_font_label;
  const wchar_t* section_key_mapping;
  const wchar_t* edit_mapping_file;
  const wchar_t* shift_label;
  const wchar_t* digraphs_label;
  const wchar_t* mapping_hint;
  // Platform-neutral variant of mapping_hint: "%ls" is replaced with the
  // real mapping file path at runtime (macOS settings window).
  const wchar_t* mapping_hint_generic;
  // Shown when a keymap.txt edit fails format validation; "%ls" is
  // replaced with the 1-based line number of the first bad line. The
  // store keeps the previous mapping and restores the file.
  const wchar_t* mapping_error_restored;
  // Caption of the "open settings" entry in the platform's input menu
  // (macOS menu-bar input menu; Windows has the hotkey/tray instead).
  const wchar_t* menu_settings;
  const wchar_t* save;
  const wchar_t* close;
  // The language's own display name ("English", "中文", ...), used as the
  // caption of the language selector.
  const wchar_t* language_name;
};

// Returns the string table for `language`.
const UiStrings& GetStrings(UiLanguage language);

// Maps an arbitrary stored value to a valid UiLanguage (out-of-range
// values fall back to English).
UiLanguage UiLanguageFromValue(uint32_t value);

// Detects the language from the OS display language (Windows: user UI
// LANGID; POSIX: LC_ALL/LC_MESSAGES/LANG): Chinese for zh, Cyrillic for
// mn, English otherwise.
UiLanguage DetectSystemLanguage();

}  // namespace paiza

#endif  // PAIZA_CORE_LOCALIZATION_H_
