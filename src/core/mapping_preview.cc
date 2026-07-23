// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid

#include "core/mapping_preview.h"

namespace paiza {

std::wstring BuildMappingPreview(const KeyMapping& mapping,
                                 const UiStrings& strings) {
  std::wstring text;
  int column = 0;
  for (char c = 'a'; c <= 'z'; ++c) {
    const std::wstring value = mapping.Lookup(std::string(1, c));
    if (value.empty()) continue;
    text.push_back(static_cast<wchar_t>(c));
    text += L" ";
    text += value;
    text += L"    ";
    if (++column % 9 == 0) text += L"\n";
  }
  text += L"\n\n";
  text += strings.shift_label;
  text += L"   ";
  for (char c = 'A'; c <= 'Z'; ++c) {
    const std::wstring upper = mapping.Lookup(std::string(1, c));
    const std::wstring lower =
        mapping.Lookup(std::string(1, static_cast<char>(c - 'A' + 'a')));
    if (upper.empty() || upper == lower) continue;  // Only the second set.
    text.push_back(static_cast<wchar_t>(c));
    text += L" ";
    text += upper;
    text += L"    ";
  }
  std::wstring sequences;
  for (const auto& [key, value] : mapping.Entries()) {
    if (key.size() < 2) continue;
    sequences += std::wstring(key.begin(), key.end());
    sequences += L" ";
    sequences += value;
    sequences += L"    ";
  }
  if (!sequences.empty()) {
    text += L"\n";
    text += strings.digraphs_label;
    text += L"   " + sequences;
  }
  return text;
}

}  // namespace paiza
