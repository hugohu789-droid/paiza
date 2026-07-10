// Copyright 2026 Paiza. All rights reserved.

#include "core/key_mapping.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "common/logging.h"

namespace paiza {
namespace {

// Lower-case letter defaults, index = letter - 'a'.
constexpr wchar_t kLowerMap[26] = {
    /* a */ 0x1820, /* b */ 0x182A, /* c */ 0x1834, /* d */ 0x1833,
    /* e */ 0x1821, /* f */ 0x1839, /* g */ 0x182D, /* h */ 0x182C,
    /* i */ 0x1822, /* j */ 0x1835, /* k */ 0x183A, /* l */ 0x182F,
    /* m */ 0x182E, /* n */ 0x1828, /* o */ 0x1823, /* p */ 0x182B,
    /* q */ 0x1827, /* r */ 0x1837, /* s */ 0x1830, /* t */ 0x1832,
    /* u */ 0x1824, /* v */ 0x1825, /* w */ 0x1838, /* x */ 0x1831,
    /* y */ 0x1836, /* z */ 0x183D,
};

// Upper-case "second set": additional Mongolian letters reached with
// Shift. Every other upper-case letter defaults to its lower-case value.
struct UpperSpecial {
  char key;
  wchar_t mongolian;
};
constexpr UpperSpecial kUpperSpecials[] = {
    {'F', 0x1839},  // FA
    {'N', 0x1829},  // ANG
    {'K', 0x183B},  // KHA
    {'C', 0x183C},  // TSA
    {'H', 0x183E},  // HAA
    {'R', 0x183F},  // ZRA
    {'L', 0x1840},  // LHA
    {'Z', 0x1841},  // ZHI
    {'Q', 0x1842},  // CHI
};

// Appends a Unicode codepoint to a UTF-16 string (with surrogate pairs).
void AppendCodepoint(std::wstring* out, unsigned long codepoint) {
  if (codepoint == 0 || codepoint > 0x10FFFF) return;
  if (codepoint <= 0xFFFF) {
    out->push_back(static_cast<wchar_t>(codepoint));
  } else {
    codepoint -= 0x10000;
    out->push_back(static_cast<wchar_t>(0xD800 + (codepoint >> 10)));
    out->push_back(static_cast<wchar_t>(0xDC00 + (codepoint & 0x3FF)));
  }
}

// Parses "1828" / "1828 180E" into UTF-16 text; empty on invalid input.
std::wstring ParseCodepoints(std::istringstream* stream) {
  std::wstring value;
  std::string token;
  while (*stream >> token) {
    char* end = nullptr;
    const unsigned long codepoint = std::strtoul(token.c_str(), &end, 16);
    if (end == nullptr || *end != '\0' || codepoint == 0 ||
        codepoint > 0x10FFFF) {
      return {};
    }
    AppendCodepoint(&value, codepoint);
  }
  return value;
}

bool IsValidKey(const std::string& key) {
  if (key.empty() || key.size() > 8) return false;
  for (const char c : key) {
    if (static_cast<unsigned char>(c) >= 128 || c == '#' ||
        std::isspace(static_cast<unsigned char>(c))) {
      return false;
    }
  }
  return true;
}

// Parses one mapping-file line (already stripped of the trailing '\r').
// kBlank covers empty and comment lines. The single parser is shared by
// LoadFromFile and ValidateKeyMapFile so the two can never drift.
enum class LineParse { kBlank, kEntry, kInvalid };

LineParse ParseKeyMapLine(const std::string& line, std::string* key,
                          std::wstring* value) {
  if (line.empty() || line[0] == '#') return LineParse::kBlank;
  // Whitespace-only lines are blank too.
  if (line.find_first_not_of(" \t") == std::string::npos) {
    return LineParse::kBlank;
  }
  std::istringstream stream(line);
  stream >> *key;
  if (!IsValidKey(*key)) return LineParse::kInvalid;
  *value = ParseCodepoints(&stream);
  if (value->empty()) return LineParse::kInvalid;
  return LineParse::kEntry;
}

}  // namespace

std::optional<KeyMapFileError> ValidateKeyMapFile(const std::wstring& path) {
  std::ifstream file(std::filesystem::path(path), std::ios::binary);
  if (!file.is_open()) return std::nullopt;  // Missing file = defaults.

  std::string line;
  size_t line_number = 0;
  while (std::getline(file, line)) {
    ++line_number;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::string key;
    std::wstring value;
    if (ParseKeyMapLine(line, &key, &value) == LineParse::kInvalid) {
      return KeyMapFileError{line_number, line};
    }
  }
  return std::nullopt;
}

// static
KeyMapping KeyMapping::CreateDefault() {
  KeyMapping mapping;
  for (int i = 0; i < 26; ++i) {
    mapping.Set(std::string(1, static_cast<char>('a' + i)),
                std::wstring(1, kLowerMap[i]));
    mapping.Set(std::string(1, static_cast<char>('A' + i)),
                std::wstring(1, kLowerMap[i]));
  }
  for (const UpperSpecial& special : kUpperSpecials) {
    mapping.Set(std::string(1, special.key),
                std::wstring(1, special.mongolian));
  }
  mapping.Set("ng", L"\x1829");  // ANG digraph.
  return mapping;
}

void KeyMapping::Set(const std::string& key, std::wstring value) {
  if (!IsValidKey(key)) return;
  if (key.size() == 1) {
    singles_[static_cast<unsigned char>(key[0])] = std::move(value);
    return;
  }
  auto it = std::find_if(
      sequences_.begin(), sequences_.end(),
      [&key](const auto& entry) { return entry.first == key; });
  if (it != sequences_.end()) {
    if (value.empty()) {
      sequences_.erase(it);
    } else {
      it->second = std::move(value);
    }
    return;
  }
  if (value.empty()) return;
  sequences_.emplace_back(key, std::move(value));
  std::stable_sort(sequences_.begin(), sequences_.end(),
                   [](const auto& a, const auto& b) {
                     return a.first.size() > b.first.size();
                   });
}

std::wstring KeyMapping::Lookup(std::string_view key) const {
  if (key.size() == 1) {
    const unsigned char c = static_cast<unsigned char>(key[0]);
    return c < 128 ? singles_[c] : std::wstring();
  }
  for (const auto& [seq, value] : sequences_) {
    if (seq == key) return value;
  }
  return {};
}

std::wstring KeyMapping::Convert(std::string_view latin) const {
  std::wstring out;
  out.reserve(latin.size());
  size_t pos = 0;
  while (pos < latin.size()) {
    // Longest sequence first (sequences_ is sorted by length descending).
    bool matched = false;
    for (const auto& [seq, value] : sequences_) {
      if (latin.compare(pos, seq.size(), seq) == 0) {
        out += value;
        pos += seq.size();
        matched = true;
        break;
      }
    }
    if (matched) continue;
    const unsigned char c = static_cast<unsigned char>(latin[pos]);
    if (c < 128 && !singles_[c].empty()) {
      out += singles_[c];
    } else {
      out.push_back(static_cast<wchar_t>(latin[pos]));
    }
    ++pos;
  }
  return out;
}

bool KeyMapping::LoadFromFile(const std::wstring& path) {
  std::ifstream file(std::filesystem::path(path), std::ios::binary);
  if (!file.is_open()) return false;

  std::string line;
  size_t loaded = 0;
  while (std::getline(file, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::string key;
    std::wstring value;
    // Malformed lines are skipped defensively here; KeyMappingStore
    // validates with ValidateKeyMapFile *before* applying a file, which
    // is where errors are reported and the file is restored.
    if (ParseKeyMapLine(line, &key, &value) != LineParse::kEntry) continue;
    Set(key, std::move(value));
    ++loaded;
  }
  Logf(LogLevel::kInfo, L"Key mapping loaded: %zu entries from %s", loaded,
       path.c_str());
  return true;
}

bool KeyMapping::SaveToFile(const std::wstring& path) const {
  // Create the parent directory (e.g. the per-user config directory).
  std::error_code ec;
  std::filesystem::create_directories(
      std::filesystem::path(path).parent_path(), ec);

  std::ofstream file(std::filesystem::path(path),
                     std::ios::binary | std::ios::trunc);
  if (!file.is_open()) return false;

  file << "# Paiza Mongolian IME key mapping.\n"
       << "# Format: <key> <hex codepoint> [<hex codepoint> ...]\n"
       << "# Keys are case-sensitive; upper-case = Shift+letter.\n"
       << "# Multi-letter keys (e.g. \"ng\") match greedily.\n"
       << "# Lines starting with '#' are comments. Saved as UTF-8.\n";
  for (const auto& [key, value] : Entries()) {
    file << key << "\t";
    bool first = true;
    // Re-encode UTF-16 (incl. surrogate pairs) back to codepoints.
    for (size_t i = 0; i < value.size(); ++i) {
      unsigned long codepoint = value[i];
      if (codepoint >= 0xD800 && codepoint <= 0xDBFF &&
          i + 1 < value.size()) {
        const unsigned long low = value[i + 1];
        if (low >= 0xDC00 && low <= 0xDFFF) {
          codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
          ++i;
        }
      }
      char buffer[16];
      std::snprintf(buffer, sizeof(buffer), "%s%04lX", first ? "" : " ",
                    codepoint);
      file << buffer;
      first = false;
    }
    file << "\n";
  }
  return true;
}

std::vector<std::pair<std::string, std::wstring>> KeyMapping::Entries()
    const {
  std::vector<std::pair<std::string, std::wstring>> entries;
  for (char c = 'a'; c <= 'z'; ++c) {
    if (!singles_[static_cast<unsigned char>(c)].empty()) {
      entries.emplace_back(std::string(1, c),
                           singles_[static_cast<unsigned char>(c)]);
    }
  }
  for (char c = 'A'; c <= 'Z'; ++c) {
    if (!singles_[static_cast<unsigned char>(c)].empty()) {
      entries.emplace_back(std::string(1, c),
                           singles_[static_cast<unsigned char>(c)]);
    }
  }
  // Any other mapped ASCII keys (punctuation etc.).
  for (int c = 0; c < 128; ++c) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) continue;
    if (!singles_[c].empty()) {
      entries.emplace_back(std::string(1, static_cast<char>(c)),
                           singles_[c]);
    }
  }
  for (const auto& entry : sequences_) entries.push_back(entry);
  return entries;
}

}  // namespace paiza
