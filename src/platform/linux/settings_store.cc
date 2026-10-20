// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid

#include "settings_store.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <string>

#include "common/paths.h"
#include "common/string_utils.h"

namespace paiza::linux_ime {
namespace {

constexpr char kKeyCandidatesPerPage[] = "candidates_per_page";
constexpr char kKeyCloudEnabled[] = "cloud_source_enabled";
constexpr char kKeyUiLanguage[] = "ui_language";
constexpr char kKeyCandidateFont[] = "candidate_font";
constexpr char kKeyEquivOu[] = "equiv_ou";
constexpr char kKeyEquivQv[] = "equiv_qv";
constexpr char kKeyEquivHg[] = "equiv_hg";
constexpr char kKeyEquivTd[] = "equiv_td";
constexpr char kKeyEquivEw[] = "equiv_ew";
constexpr char kKeyActiveProfile[] = "active_profile";

constexpr std::chrono::milliseconds kCheckInterval{500};

// Trims ASCII whitespace from both ends.
std::string Trim(const std::string& s) {
  const size_t begin = s.find_first_not_of(" \t\r");
  if (begin == std::string::npos) return {};
  const size_t end = s.find_last_not_of(" \t\r");
  return s.substr(begin, end - begin + 1);
}

bool ParseBool(const std::string& value) {
  return value == "1" || value == "true";
}

}  // namespace

std::wstring SettingsFilePath() {
  const std::wstring dir = UserConfigDirectory();
  if (dir.empty()) return {};
  return (std::filesystem::path(dir) / L"settings.ini").wstring();
}

Settings SettingsStore::Load() { return LoadFromFile(SettingsFilePath()); }

Settings SettingsStore::LoadFromFile(const std::wstring& path) {
  Settings settings;
  settings.ui_language = DetectSystemLanguage();
  if (path.empty()) return settings;
  std::ifstream file(std::filesystem::path(path), std::ios::binary);
  if (!file.is_open()) return settings;

  std::string line;
  while (std::getline(file, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const std::string trimmed = Trim(line);
    if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';') continue;
    const size_t equals = trimmed.find('=');
    if (equals == std::string::npos) continue;
    const std::string key = Trim(trimmed.substr(0, equals));
    const std::string value = Trim(trimmed.substr(equals + 1));
    if (key == kKeyCandidatesPerPage) {
      const long parsed = std::strtol(value.c_str(), nullptr, 10);
      settings.candidates_per_page =
          static_cast<uint32_t>(std::clamp<long>(parsed, 1, 9));
    } else if (key == kKeyCloudEnabled) {
      settings.cloud_source_enabled = ParseBool(value);
    } else if (key == kKeyUiLanguage) {
      settings.ui_language = UiLanguageFromValue(
          static_cast<uint32_t>(std::strtoul(value.c_str(), nullptr, 10)));
    } else if (key == kKeyCandidateFont) {
      if (!value.empty()) settings.candidate_font = Utf8ToWide(value);
    } else if (key == kKeyEquivOu) {
      settings.equiv_ou = ParseBool(value);
    } else if (key == kKeyEquivQv) {
      settings.equiv_qv = ParseBool(value);
    } else if (key == kKeyEquivHg) {
      settings.equiv_hg = ParseBool(value);
    } else if (key == kKeyEquivTd) {
      settings.equiv_td = ParseBool(value);
    } else if (key == kKeyEquivEw) {
      settings.equiv_ew = ParseBool(value);
    } else if (key == kKeyActiveProfile) {
      if (!value.empty()) settings.active_profile = value;
    }
  }
  return settings;
}

bool SettingsStore::Save(const Settings& settings) {
  return SaveToFile(settings, SettingsFilePath());
}

bool SettingsStore::SaveToFile(const Settings& settings,
                               const std::wstring& path) {
  if (path.empty()) return false;
  std::error_code ec;
  std::filesystem::create_directories(
      std::filesystem::path(path).parent_path(), ec);
  std::ofstream file(std::filesystem::path(path),
                     std::ios::binary | std::ios::trunc);
  if (!file.is_open()) return false;
  file << "# Paiza Mongolian IME settings. Values reload within a second\n"
       << "# of saving; invalid values fall back to the defaults.\n"
       << kKeyCandidatesPerPage << "=" << settings.candidates_per_page
       << "\n"
       << kKeyCloudEnabled << "=" << (settings.cloud_source_enabled ? 1 : 0)
       << "\n"
       << kKeyUiLanguage << "="
       << static_cast<uint32_t>(settings.ui_language) << "\n"
       << kKeyCandidateFont << "=" << WideToUtf8(settings.candidate_font)
       << "\n"
       << kKeyEquivOu << "=" << (settings.equiv_ou ? 1 : 0) << "\n"
       << kKeyEquivQv << "=" << (settings.equiv_qv ? 1 : 0) << "\n"
       << kKeyEquivHg << "=" << (settings.equiv_hg ? 1 : 0) << "\n"
       << kKeyEquivTd << "=" << (settings.equiv_td ? 1 : 0) << "\n"
       << kKeyEquivEw << "=" << (settings.equiv_ew ? 1 : 0) << "\n"
       << kKeyActiveProfile << "=" << settings.active_profile << "\n";
  return file.good();
}

SettingsWatcher::SettingsWatcher() : SettingsWatcher(SettingsFilePath()) {}

SettingsWatcher::SettingsWatcher(std::wstring path)
    : path_(std::move(path)),
      settings_(SettingsStore::LoadFromFile(path_)) {}

const Settings& SettingsWatcher::Get() {
  const auto now = std::chrono::steady_clock::now();
  if (now < next_check_) return settings_;
  next_check_ = now + kCheckInterval;
  if (path_.empty()) return settings_;
  std::error_code ec;
  auto current = std::filesystem::last_write_time(path_, ec);
  if (ec) current = std::filesystem::file_time_type::min();
  if (current != last_write_) {
    last_write_ = current;
    settings_ = SettingsStore::LoadFromFile(path_);
  }
  return settings_;
}

}  // namespace paiza::linux_ime
