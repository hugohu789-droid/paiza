// Copyright 2026 Paiza. All rights reserved.

#include "core/localization.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <clocale>
#include <cstdlib>
#include <cstring>
#endif

namespace paiza {
namespace {

constexpr UiStrings kEnglishStrings = {
    /*window_title=*/L"Paiza Mongolian IME Settings",
    /*app_title=*/L"Paiza Mongolian IME",
    /*subtitle=*/L"Traditional Mongolian input · vertical script",
    /*section_general=*/L"GENERAL",
    /*candidates_per_page=*/L"Candidates per page",
    /*cloud_candidates=*/L"Cloud candidates (preview)",
    /*toggle_on=*/L"On",
    /*toggle_off=*/L"Off",
    /*language_label=*/L"Language",
    /*candidate_font_label=*/L"Candidate window font",
    /*section_key_mapping=*/
    L"KEY MAPPING   ·   upper-case = Shift second set",
    /*edit_mapping_file=*/L"Edit mapping file…",
    /*shift_label=*/L"Shift:",
    /*digraphs_label=*/L"Digraphs:",
    /*mapping_hint=*/
    L"Saved to %APPDATA%\\PaizaIME\\keymap.txt — changes apply on the "
    L"next keystroke.",
    /*save=*/L"Save",
    /*close=*/L"Close",
    /*language_name=*/L"English",
};

constexpr UiStrings kChineseStrings = {
    /*window_title=*/L"Paiza 蒙古文输入法设置",
    /*app_title=*/L"Paiza Mongolian IME",
    /*subtitle=*/L"传统蒙古文输入法 · 竖排文字",
    /*section_general=*/L"常规",
    /*candidates_per_page=*/L"每页候选词数",
    /*cloud_candidates=*/L"云端候选词（预览）",
    /*toggle_on=*/L"开",
    /*toggle_off=*/L"关",
    /*language_label=*/L"界面语言",
    /*candidate_font_label=*/L"候选框字体",
    /*section_key_mapping=*/L"按键映射   ·   大写字母 = Shift 第二组",
    /*edit_mapping_file=*/L"编辑映射文件…",
    /*shift_label=*/L"Shift:",
    /*digraphs_label=*/L"组合键:",
    /*mapping_hint=*/
    L"保存于 %APPDATA%\\PaizaIME\\keymap.txt —— 保存后下一次按键即生效。",
    /*save=*/L"保存",
    /*close=*/L"关闭",
    /*language_name=*/L"中文",
};

constexpr UiStrings kCyrillicStrings = {
    /*window_title=*/L"Paiza Монгол бичгийн IME — Тохиргоо",
    /*app_title=*/L"Paiza Mongolian IME",
    /*subtitle=*/L"Монгол бичгийн оруулах систем · босоо бичиг",
    /*section_general=*/L"ЕРӨНХИЙ",
    /*candidates_per_page=*/L"Хуудас бүрийн үгийн тоо",
    /*cloud_candidates=*/L"Үүлэн санал (туршилт)",
    /*toggle_on=*/L"Идэвхтэй",
    /*toggle_off=*/L"Идэвхгүй",
    /*language_label=*/L"Хэл",
    /*candidate_font_label=*/L"Үгийн фонт",
    /*section_key_mapping=*/
    L"ТОВЧЛУУРЫН ХОЛБОЛТ   ·   том үсэг = Shift хоёр дахь багц",
    /*edit_mapping_file=*/L"Холболтын файл засах…",
    /*shift_label=*/L"Shift:",
    /*digraphs_label=*/L"Хос үсэг:",
    /*mapping_hint=*/
    L"%APPDATA%\\PaizaIME\\keymap.txt — хадгалсны дараа дараагийн "
    L"даралтаас хүчинтэй.",
    /*save=*/L"Хадгалах",
    /*close=*/L"Хаах",
    /*language_name=*/L"Монгол кирилл",
};

}  // namespace

const UiStrings& GetStrings(UiLanguage language) {
  switch (language) {
    case UiLanguage::kChinese:
      return kChineseStrings;
    case UiLanguage::kCyrillic:
      return kCyrillicStrings;
    case UiLanguage::kEnglish:
      break;
  }
  return kEnglishStrings;
}

UiLanguage UiLanguageFromValue(uint32_t value) {
  return value < kUiLanguageCount ? static_cast<UiLanguage>(value)
                                  : UiLanguage::kEnglish;
}

UiLanguage DetectSystemLanguage() {
#if defined(_WIN32)
  switch (PRIMARYLANGID(::GetUserDefaultUILanguage())) {
    case LANG_CHINESE:
      return UiLanguage::kChinese;
    case LANG_MONGOLIAN:
      // Mongolian OS installs use Cyrillic UI (mn-MN).
      return UiLanguage::kCyrillic;
    default:
      // Unsupported OS languages fall back to English (spec default).
      return UiLanguage::kEnglish;
  }
#else
  // POSIX: first defined of LC_ALL / LC_MESSAGES / LANG, e.g. "zh_CN.UTF-8".
  const char* locale = std::getenv("LC_ALL");
  if (locale == nullptr || *locale == '\0') {
    locale = std::getenv("LC_MESSAGES");
  }
  if (locale == nullptr || *locale == '\0') locale = std::getenv("LANG");
  if (locale != nullptr) {
    if (std::strncmp(locale, "zh", 2) == 0) return UiLanguage::kChinese;
    if (std::strncmp(locale, "mn", 2) == 0) return UiLanguage::kCyrillic;
  }
  return UiLanguage::kEnglish;
#endif
}

}  // namespace paiza
