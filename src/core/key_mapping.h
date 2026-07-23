// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid
//
// User-editable keyboard mapping: Latin key sequences -> Mongolian text.
// Keys are case-sensitive single ASCII characters ("a", "N") or multi-char
// sequences ("ng"). Shift+letter produces the upper-case key, giving every
// physical key a second, independently mappable Mongolian letter.
//
// The mapping lives in a plain text file so users can edit it from the
// settings dialog (spec: users can edit the key-to-letter mapping
// themselves, saved to a separate config file). Format, one entry per line,
// '#' starts a comment:
//
//   <key> <TAB-or-spaces> <hex codepoint> [<hex codepoint> ...]
//   n     1828
//   N     1829
//   ng    1829

#ifndef PAIZA_CORE_KEY_MAPPING_H_
#define PAIZA_CORE_KEY_MAPPING_H_

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace paiza {

// First format error of a mapping file (1-based line number + the line's
// text), reported by ValidateKeyMapFile.
struct KeyMapFileError {
  size_t line_number = 0;
  std::string line;
};

// Validates the mapping file's format without applying it: every
// non-blank, non-comment line must be `<key> <hex codepoint>...` with a
// valid key (ASCII, no spaces/'#', at most 8 chars) and at least one
// valid codepoint. Returns the first offending line, or nullopt when the
// file is well-formed. A missing/unreadable file is valid (the built-in
// defaults apply then).
std::optional<KeyMapFileError> ValidateKeyMapFile(const std::wstring& path);

class KeyMapping {
 public:
  KeyMapping() = default;

  // Built-in mapping: 26 lower-case letters, upper-case letters defaulting
  // to their lower-case value except the second-set specials
  // (N K C H R L Z Q F), plus the "ng" digraph.
  static KeyMapping CreateDefault();

  // Converts a typed key sequence to Mongolian text. Longest mapped
  // sequence wins at each position; unmapped characters pass through.
  std::wstring Convert(std::string_view latin) const;

  // Returns the mapped text for an exact key ("a", "N", "ng"); empty if
  // the key is not mapped.
  std::wstring Lookup(std::string_view key) const;

  // Adds or replaces one entry. Empty `value` removes the entry.
  void Set(const std::string& key, std::wstring value);

  // Overlays entries from the config file onto the current mapping.
  // Returns false if the file cannot be opened (mapping unchanged then).
  bool LoadFromFile(const std::wstring& path);

  // Writes the full mapping (with a format header) to the config file,
  // creating the parent directory if needed.
  bool SaveToFile(const std::wstring& path) const;

  // All entries: single-char keys first (a-z, A-Z order), then sequences.
  std::vector<std::pair<std::string, std::wstring>> Entries() const;

 private:
  // Mapped values for single ASCII keys, indexed by character.
  std::array<std::wstring, 128> singles_;
  // Multi-character keys, kept sorted by descending key length so Convert
  // can greedily match the longest sequence first.
  std::vector<std::pair<std::string, std::wstring>> sequences_;
};

}  // namespace paiza

#endif  // PAIZA_CORE_KEY_MAPPING_H_
