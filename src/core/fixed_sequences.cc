// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid

#include "core/fixed_sequences.h"

#include <cstdlib>
#include <sstream>

namespace paiza {
namespace {

constexpr wchar_t kMvs = 0x180E;
constexpr wchar_t kNnbsp = 0x202F;

// Vowel harmony: masculine (back) vs feminine (front) vowels.
enum class Gender { kMasculine, kFeminine };

bool IsMasculineVowel(wchar_t cp) {
  return cp == 0x1820 || cp == 0x1823 || cp == 0x1824;  // a, o, u.
}
bool IsFeminineVowel(wchar_t cp) {
  return cp == 0x1821 || cp == 0x1825 || cp == 0x1826;  // e, oe, ue.
}

// The stem's gender = the first non-neutral vowel it contains (Mongolian
// harmony is set by the stem); default masculine when it has only neutral
// vowels (i) or none.
Gender StemGender(const std::wstring& text, size_t end) {
  for (size_t i = 0; i < end; ++i) {
    if (IsMasculineVowel(text[i])) return Gender::kMasculine;
    if (IsFeminineVowel(text[i])) return Gender::kFeminine;
  }
  return Gender::kMasculine;
}

// Maps an a/e or u/ue vowel to the given gender; other codepoints unchanged.
wchar_t Harmonize(wchar_t cp, Gender gender) {
  if (cp == 0x1820 || cp == 0x1821)  // a / e.
    return gender == Gender::kMasculine ? 0x1820 : 0x1821;
  if (cp == 0x1824 || cp == 0x1826)  // u / ue.
    return gender == Gender::kMasculine ? 0x1824 : 0x1826;
  return cp;
}

}  // namespace

void FixedSequenceTable::LoadFromString(const std::string& text) {
  suffixes_.clear();
  std::istringstream stream(text);
  std::string line;
  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::istringstream tokens(line);
    std::string token;
    bool has_mvs = false;
    bool first = true;
    SuffixForm form;
    bool commented = false;
    while (tokens >> token) {
      if (token[0] == '#') break;  // Rest of the line is a comment.
      if (first && token == "MVS") {
        has_mvs = true;
        first = false;
        continue;
      }
      first = false;
      // A position: "1820" or "1820|1821".
      std::vector<wchar_t> alternatives;
      size_t start = 0;
      while (start <= token.size()) {
        const size_t bar = token.find('|', start);
        const std::string hex = token.substr(
            start, bar == std::string::npos ? std::string::npos : bar - start);
        if (!hex.empty()) {
          const long cp = std::strtol(hex.c_str(), nullptr, 16);
          if (cp > 0) alternatives.push_back(static_cast<wchar_t>(cp));
        }
        if (bar == std::string::npos) break;
        start = bar + 1;
      }
      if (!alternatives.empty()) form.push_back(std::move(alternatives));
    }
    (void)commented;
    // Only MVS-dependent forms of length >= 2 are used for insertion (a lone
    // vowel would match far too many stem endings).
    if (has_mvs && form.size() >= 2) suffixes_.push_back(std::move(form));
  }
}

std::wstring FixedSequenceTable::InsertSuffixMvs(
    const std::wstring& text) const {
  const size_t n = text.size();
  const SuffixForm* best = nullptr;
  size_t best_start = 0;
  size_t best_len = 0;

  for (const SuffixForm& form : suffixes_) {
    const size_t len = form.size();
    if (len < 2 || len + 1 > n) continue;  // Need at least one stem codepoint.
    const size_t start = n - len;
    if (start == 0) continue;  // No stem before the suffix.
    const wchar_t before = text[start - 1];
    if (before == kMvs || before == kNnbsp) continue;  // Already separated.

    bool match = true;
    for (size_t i = 0; i < len && match; ++i) {
      const wchar_t cp = text[start + i];
      bool in_position = false;
      for (wchar_t alt : form[i]) {
        if (alt == cp) {
          in_position = true;
          break;
        }
      }
      if (!in_position) match = false;
    }
    if (match && len > best_len) {
      best = &form;
      best_start = start;
      best_len = len;
    }
  }

  if (best == nullptr) return text;

  const Gender gender = StemGender(text, best_start);
  std::wstring out = text.substr(0, best_start);
  out.push_back(kMvs);
  for (size_t i = 0; i < best_len; ++i) {
    out.push_back(Harmonize(text[best_start + i], gender));
  }
  return out;
}

std::wstring FixedSequenceTable::StandardizeNnbspToMvs(
    const std::wstring& token) const {
  const size_t pos = token.find(kNnbsp);
  if (pos == std::wstring::npos || pos == 0 || pos + 1 >= token.size()) {
    return token;  // No NNBSP, or no stem / no suffix around it.
  }
  const std::wstring suffix = token.substr(pos + 1);
  for (const SuffixForm& form : suffixes_) {
    if (form.size() != suffix.size()) continue;
    bool match = true;
    for (size_t i = 0; i < form.size() && match; ++i) {
      bool in_position = false;
      for (wchar_t alt : form[i]) {
        if (alt == suffix[i]) {
          in_position = true;
          break;
        }
      }
      if (!in_position) match = false;
    }
    if (!match) continue;
    // A standard MVS-suffix mis-encoded with NNBSP: fix the joiner + harmony.
    const Gender gender = StemGender(token, pos);
    std::wstring out = token.substr(0, pos);
    out.push_back(kMvs);
    for (wchar_t cp : suffix) out.push_back(Harmonize(cp, gender));
    return out;
  }
  return token;  // NNBSP not a standard suffix (genuine enclitic): keep it.
}

}  // namespace paiza
