// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid
//
// Thin facade over the built-in default KeyMapping. This is the "simple
// algorithm" fallback used when neither the candidate service nor a user
// mapping file is reachable. Runtime conversion in the TSF module goes
// through KeyMappingStore (hot-reloadable, user-editable) instead.

#ifndef PAIZA_CORE_TRANSLITERATOR_H_
#define PAIZA_CORE_TRANSLITERATOR_H_

#include <string>
#include <string_view>

namespace paiza {

class Transliterator {
 public:
  // Converts a case-sensitive Latin key sequence with the built-in default
  // mapping: "monggol" -> U+182E U+1823 U+1829 U+182D U+1823 U+182F.
  // Upper-case keys select the second letter set (e.g. "N" -> U+1829 ANG).
  // The "ng" digraph is consumed greedily before single letters; unmapped
  // characters are copied through unchanged.
  static std::wstring Convert(std::string_view latin);

  // Returns true if `ch` participates in composition (i.e. the IME should
  // consume the keystroke and append it to the preedit buffer).
  static bool IsComposableChar(char ch);
};

}  // namespace paiza

#endif  // PAIZA_CORE_TRANSLITERATOR_H_
