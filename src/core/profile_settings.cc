// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid

#include "core/profile_settings.h"

#include <vector>

#include "common/string_utils.h"
#include "core/input_profile.h"

namespace paiza {
namespace {

constexpr wchar_t kEntrySeparator = L';';
constexpr wchar_t kPairSeparator = L'=';

struct Entry {
  std::wstring id;
  std::wstring value;
};

// Splits a packed string. A string with no '=' anywhere is a plain value
// from before this was per profile; the caller handles that case, so it is
// reported rather than guessed at here.
std::vector<Entry> Unpack(std::wstring_view packed, bool* legacy) {
  *legacy = packed.find(kPairSeparator) == std::wstring_view::npos &&
            !packed.empty();
  std::vector<Entry> entries;
  if (*legacy) return entries;

  size_t begin = 0;
  while (begin <= packed.size() && !packed.empty()) {
    const size_t end = packed.find(kEntrySeparator, begin);
    const std::wstring_view item = packed.substr(
        begin, end == std::wstring_view::npos ? std::wstring_view::npos
                                              : end - begin);
    const size_t eq = item.find(kPairSeparator);
    if (eq != std::wstring_view::npos && eq > 0) {
      entries.push_back({std::wstring(item.substr(0, eq)),
                         std::wstring(item.substr(eq + 1))});
    }
    if (end == std::wstring_view::npos) break;
    begin = end + 1;
  }
  return entries;
}

std::wstring Pack(const std::vector<Entry>& entries) {
  std::wstring out;
  for (const Entry& entry : entries) {
    if (entry.value.empty()) continue;
    if (!out.empty()) out += kEntrySeparator;
    out += entry.id;
    out += kPairSeparator;
    out += entry.value;
  }
  return out;
}

}  // namespace

bool IsStorablePerProfileValue(std::wstring_view value) {
  return value.find(kEntrySeparator) == std::wstring_view::npos;
}

std::wstring PerProfileValue(std::wstring_view packed,
                             std::string_view profile_id) {
  bool legacy = false;
  const std::vector<Entry> entries = Unpack(packed, &legacy);
  // One value and no profile ids: what the user chose before the setting
  // was per profile. It answers for the **default** profile only, because
  // that is the only profile that existed when they chose it.
  //
  // Letting it answer for every profile was wrong in a way that is worse
  // than losing a preference: a ZCode profile would inherit a Unicode
  // Mongolian font and override the form-code font it declares, and ZCode
  // in a Unicode font renders as the Sibe and Manchu letters those code
  // points are in Unicode. The wrong font there is wrong *text*.
  if (legacy) {
    return profile_id == kDefaultProfileId ? std::wstring(packed)
                                           : std::wstring();
  }

  const std::wstring wanted = Utf8ToWide(std::string(profile_id));
  for (const Entry& entry : entries) {
    if (entry.id == wanted) return entry.value;
  }
  return std::wstring();
}

std::wstring SetPerProfileValue(std::wstring_view packed,
                                std::string_view profile_id,
                                std::wstring_view value) {
  if (!IsStorablePerProfileValue(value)) return std::wstring(packed);
  const std::wstring wanted = Utf8ToWide(std::string(profile_id));
  if (wanted.empty() || !IsStorablePerProfileValue(wanted) ||
      wanted.find(kPairSeparator) != std::wstring::npos) {
    return std::wstring(packed);
  }

  bool legacy = false;
  std::vector<Entry> entries = Unpack(packed, &legacy);
  if (legacy) {
    // Migrate: the old single value belonged to the default profile, which
    // was the only one that existed when it was chosen, so that is where
    // it goes -- not to whichever profile happens to be written first,
    // which would silently lose it the moment someone picked a font for
    // ZCode.
    entries.push_back({Utf8ToWide(std::string(kDefaultProfileId)),
                       std::wstring(packed)});
  }
  for (Entry& entry : entries) {
    if (entry.id == wanted) {
      entry.value = std::wstring(value);
      return Pack(entries);
    }
  }
  entries.push_back({wanted, std::wstring(value)});
  return Pack(entries);
}

}  // namespace paiza
