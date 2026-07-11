// Copyright 2026 Paiza. All rights reserved.

#include "core/romanization.h"

#include <map>

#include "core/key_mapping.h"

namespace paiza {
namespace {

// codepoint -> latin token, inverse of the default keymap. Built once.
class InverseTable {
 public:
  static const InverseTable& Get() {
    static const InverseTable table;
    return table;
  }

  const std::string* Find(wchar_t cp) const {
    auto it = map_.find(cp);
    return it == map_.end() ? nullptr : &it->second;
  }

 private:
  InverseTable() {
    const KeyMapping defaults = KeyMapping::CreateDefault();
    for (const auto& [latin, mongolian] : defaults.Entries()) {
      if (mongolian.size() != 1 || latin.empty()) continue;
      const wchar_t cp = mongolian[0];
      auto it = map_.find(cp);
      if (it == map_.end()) {
        map_.emplace(cp, latin);
      } else if (PreferLower(latin, it->second)) {
        it->second = latin;  // Prefer a single lower-case token (f over F).
      }
    }
  }

  // A single lower-case-letter token is preferred as the canonical one.
  static bool PreferLower(const std::string& candidate,
                          const std::string& current) {
    const bool cand_lower =
        candidate.size() == 1 && candidate[0] >= 'a' && candidate[0] <= 'z';
    const bool cur_lower =
        current.size() == 1 && current[0] >= 'a' && current[0] <= 'z';
    return cand_lower && !cur_lower;
  }

  std::map<wchar_t, std::string> map_;
};

}  // namespace

std::string Romanize(const std::wstring& text) {
  const InverseTable& table = InverseTable::Get();
  std::string out;
  out.reserve(text.size());
  for (wchar_t cp : text) {
    if (const std::string* token = table.Find(cp)) {
      out += *token;
    } else if (cp < 0x80) {
      out.push_back(static_cast<char>(cp));  // Pass ASCII through unchanged.
    }
    // Other unmapped codepoints are dropped.
  }
  return out;
}

bool ContainsMongolianLetter(const std::wstring& text) {
  for (wchar_t cp : text) {
    if (cp >= 0x1820 && cp <= 0x1842) return true;
  }
  return false;
}

}  // namespace paiza
