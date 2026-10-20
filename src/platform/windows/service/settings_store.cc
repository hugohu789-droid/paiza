// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid

#include "service/settings_store.h"

#include "common/string_utils.h"

#include <windows.h>

#include <algorithm>

namespace paiza::service {
namespace {

constexpr wchar_t kRegistryPath[] = L"Software\\PaizaIME";
constexpr wchar_t kValuePageSize[] = L"CandidatesPerPage";
constexpr wchar_t kValueCloudEnabled[] = L"CloudSourceEnabled";
constexpr wchar_t kValueUiLanguage[] = L"UiLanguage";
constexpr wchar_t kValueCandidateFont[] = L"CandidateFont";
constexpr wchar_t kValueEquivOu[] = L"EquivOU";
constexpr wchar_t kValueEquivQv[] = L"EquivQV";
constexpr wchar_t kValueEquivHg[] = L"EquivHG";
constexpr wchar_t kValueEquivTd[] = L"EquivTD";
constexpr wchar_t kValueEquivEw[] = L"EquivEW";
constexpr wchar_t kValueActiveProfile[] = L"ActiveProfile";

uint32_t ReadDword(HKEY key, const wchar_t* name, uint32_t fallback) {
  DWORD value = 0;
  DWORD size = sizeof(value);
  DWORD type = 0;
  if (::RegQueryValueExW(key, name, nullptr, &type,
                         reinterpret_cast<BYTE*>(&value),
                         &size) == ERROR_SUCCESS &&
      type == REG_DWORD) {
    return value;
  }
  return fallback;
}

void WriteDword(HKEY key, const wchar_t* name, uint32_t value) {
  ::RegSetValueExW(key, name, 0, REG_DWORD,
                   reinterpret_cast<const BYTE*>(&value), sizeof(value));
}

std::wstring ReadString(HKEY key, const wchar_t* name,
                        const std::wstring& fallback) {
  wchar_t buffer[256] = {};
  DWORD size = sizeof(buffer) - sizeof(wchar_t);
  DWORD type = 0;
  if (::RegQueryValueExW(key, name, nullptr, &type,
                         reinterpret_cast<BYTE*>(buffer),
                         &size) == ERROR_SUCCESS &&
      type == REG_SZ && buffer[0] != L'\0') {
    return buffer;
  }
  return fallback;
}

void WriteString(HKEY key, const wchar_t* name, const std::wstring& value) {
  ::RegSetValueExW(
      key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
      static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
}

}  // namespace

Settings SettingsStore::Load() {
  Settings settings;
  // The language default follows the OS display language until the user
  // explicitly saves a choice.
  settings.ui_language = DetectSystemLanguage();
  HKEY key = nullptr;
  if (::RegOpenKeyExW(HKEY_CURRENT_USER, kRegistryPath, 0, KEY_READ,
                      &key) == ERROR_SUCCESS) {
    settings.candidates_per_page =
        ReadDword(key, kValuePageSize, settings.candidates_per_page);
    settings.cloud_source_enabled =
        ReadDword(key, kValueCloudEnabled, 0) != 0;
    settings.ui_language = UiLanguageFromValue(
        ReadDword(key, kValueUiLanguage,
                  static_cast<uint32_t>(settings.ui_language)));
    settings.candidate_font =
        ReadString(key, kValueCandidateFont, settings.candidate_font);
    settings.equiv_ou = ReadDword(key, kValueEquivOu, 1) != 0;
    settings.equiv_qv = ReadDword(key, kValueEquivQv, 1) != 0;
    settings.equiv_hg = ReadDword(key, kValueEquivHg, 1) != 0;
    settings.equiv_td = ReadDword(key, kValueEquivTd, 1) != 0;
    settings.equiv_ew = ReadDword(key, kValueEquivEw, 1) != 0;
    settings.active_profile = WideToUtf8(ReadString(
        key, kValueActiveProfile, Utf8ToWide(settings.active_profile)));
    ::RegCloseKey(key);
  }
  settings.candidates_per_page =
      std::clamp<uint32_t>(settings.candidates_per_page, 1, 9);
  return settings;
}

void SettingsStore::Save(const Settings& settings) {
  HKEY key = nullptr;
  if (::RegCreateKeyExW(HKEY_CURRENT_USER, kRegistryPath, 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &key,
                        nullptr) == ERROR_SUCCESS) {
    WriteDword(key, kValuePageSize, settings.candidates_per_page);
    WriteDword(key, kValueCloudEnabled,
               settings.cloud_source_enabled ? 1 : 0);
    WriteDword(key, kValueUiLanguage,
               static_cast<uint32_t>(settings.ui_language));
    WriteString(key, kValueCandidateFont, settings.candidate_font);
    WriteDword(key, kValueEquivOu, settings.equiv_ou ? 1 : 0);
    WriteDword(key, kValueEquivQv, settings.equiv_qv ? 1 : 0);
    WriteDword(key, kValueEquivHg, settings.equiv_hg ? 1 : 0);
    WriteDword(key, kValueEquivTd, settings.equiv_td ? 1 : 0);
    WriteDword(key, kValueEquivEw, settings.equiv_ew ? 1 : 0);
    WriteString(key, kValueActiveProfile, Utf8ToWide(settings.active_profile));
    ::RegCloseKey(key);
  }
}

}  // namespace paiza::service
