// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid

#include "core/input_profile.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "common/string_utils.h"

namespace paiza {
namespace {

// Trims ASCII whitespace (and a trailing CR from CRLF files) from both ends.
std::string Trim(const std::string& text) {
  size_t begin = 0;
  size_t end = text.size();
  auto is_space = [](char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
  };
  while (begin < end && is_space(text[begin])) ++begin;
  while (end > begin && is_space(text[end - 1])) --end;
  return text.substr(begin, end - begin);
}

// "yes"/"true"/"1" are true, "no"/"false"/"0" are false, anything else keeps
// the caller's default so an unrecognised value never silently flips behaviour.
bool ParseBool(const std::string& text, bool fallback) {
  std::string lower;
  lower.reserve(text.size());
  for (char c : text) {
    lower.push_back(
        static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c));
  }
  if (lower == "yes" || lower == "true" || lower == "1") return true;
  if (lower == "no" || lower == "false" || lower == "0") return false;
  return fallback;
}

}  // namespace

InlineComposition ParseInlineComposition(std::string_view text,
                                         InlineComposition fallback) {
  if (text == "full") return InlineComposition::kFull;
  if (text == "placeholder") return InlineComposition::kPlaceholder;
  if (text == "hidden") return InlineComposition::kHidden;
  return fallback;
}

std::string InlineCompositionName(InlineComposition value) {
  switch (value) {
    case InlineComposition::kPlaceholder:
      return "placeholder";
    case InlineComposition::kHidden:
      return "hidden";
    case InlineComposition::kFull:
      break;
  }
  return "full";
}

InlineComposition InlineCompositionFromValue(uint32_t value) {
  switch (value) {
    case 1:
      return InlineComposition::kPlaceholder;
    case 2:
      return InlineComposition::kHidden;
    default:
      return InlineComposition::kFull;
  }
}

InputProfile BuiltInUnicodeProfile() {
  InputProfile profile;
  profile.id = kDefaultProfileId;
  profile.display_name = L"Unicode (GB/T)";
  profile.keymap = L"keymap.txt";
  profile.lexicon = L"lexicon.bin";
  profile.user_db = L"user.db";
  profile.font = L"Noto Sans Mongolian";
  profile.fixed_sequences = true;
  profile.romanize_query = true;
  profile.skeleton_input = true;
  profile.vowel_keys = "aeioquvE";
  profile.inline_default = InlineComposition::kFull;
  return profile;
}

void InputProfileRegistry::EnsureDefault() {
  if (profiles_.empty()) profiles_.push_back(BuiltInUnicodeProfile());
}

bool InputProfileRegistry::LoadFromString(const std::string& content) {
  InputProfile profile;
  std::istringstream stream(content);
  std::string line;
  while (std::getline(stream, line)) {
    const std::string trimmed = Trim(line);
    // Whole-line comments only: values such as a font family may legitimately
    // contain '#', so a mid-line '#' is not treated as a comment marker.
    if (trimmed.empty() || trimmed[0] == '#') continue;

    // "key value": the key is the first token, the value is the rest of the
    // line, so font families with spaces survive intact.
    const size_t split = trimmed.find_first_of(" \t");
    if (split == std::string::npos) continue;  // Key with no value: ignore.
    const std::string key = trimmed.substr(0, split);
    const std::string value = Trim(trimmed.substr(split + 1));
    if (value.empty()) continue;

    if (key == "id") {
      profile.id = value;
    } else if (key == "display_name") {
      profile.display_name = Utf8ToWide(value);
    } else if (key == "keymap") {
      profile.keymap = Utf8ToWide(value);
    } else if (key == "lexicon") {
      profile.lexicon = Utf8ToWide(value);
    } else if (key == "user_db") {
      profile.user_db = Utf8ToWide(value);
    } else if (key == "font") {
      profile.font = Utf8ToWide(value);
    } else if (key == "fixed_sequences") {
      profile.fixed_sequences = ParseBool(value, profile.fixed_sequences);
    } else if (key == "romanize_query") {
      profile.romanize_query = ParseBool(value, profile.romanize_query);
    } else if (key == "skeleton_input") {
      profile.skeleton_input = ParseBool(value, profile.skeleton_input);
    } else if (key == "vowel_keys") {
      profile.vowel_keys = value;
    } else if (key == "inline_default") {
      profile.inline_default =
          ParseInlineComposition(value, profile.inline_default);
    }
    // Unknown keys are ignored so a newer profile file stays loadable by an
    // older build.
  }

  if (profile.id.empty()) return false;  // An id is what settings store.
  if (profile.display_name.empty()) profile.display_name = Utf8ToWide(profile.id);

  const auto existing =
      std::find_if(profiles_.begin(), profiles_.end(),
                   [&](const InputProfile& p) { return p.id == profile.id; });
  if (existing != profiles_.end()) {
    *existing = std::move(profile);
  } else {
    profiles_.push_back(std::move(profile));
  }
  return true;
}

int InputProfileRegistry::LoadFromDirectory(const std::wstring& dir) {
  std::error_code ec;
  const std::filesystem::path root(dir);
  if (!std::filesystem::is_directory(root, ec)) return 0;

  // Sort by file name so the settings dropdown has a stable order.
  std::vector<std::filesystem::path> files;
  for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
    if (ec) break;
    if (!entry.is_regular_file(ec)) continue;
    if (entry.path().extension() != ".txt") continue;
    files.push_back(entry.path());
  }
  std::sort(files.begin(), files.end());

  int loaded = 0;
  for (const auto& file : files) {
    std::ifstream stream(file, std::ios::binary);
    if (!stream.is_open()) continue;
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    if (LoadFromString(buffer.str())) ++loaded;
  }
  return loaded;
}

const InputProfile* InputProfileRegistry::Find(std::string_view id) const {
  for (const InputProfile& profile : profiles_) {
    if (profile.id == id) return &profile;
  }
  return nullptr;
}

}  // namespace paiza
