// Copyright 2026 Paiza. All rights reserved.
//
// Middle-layer romanization: a fixed, lossless map from Mongolian codepoints
// to latin tokens, defined as the inverse of the *default* key mapping. It
// decouples dictionary keys from a user-editable keymap:
//
//   dictionary key = Romanize(Mongolian word)          (compile time)
//   query key      = Romanize(userKeymap.Convert(keys)) (query time)
//
// Because the table is the inverse of the default layout, Romanize(Convert(x))
// == x for the default keymap (a no-op), while a remapped keymap still lands
// on the same canonical key. The table is a built-in (both compiler and
// runtime derive it from KeyMapping::CreateDefault()), so it is identical
// everywhere without being embedded.

#ifndef PAIZA_CORE_ROMANIZATION_H_
#define PAIZA_CORE_ROMANIZATION_H_

#include <string>

namespace paiza {

// Maps each codepoint of `text` to its default-layout latin token; format /
// control characters (BIRGA, NIRUGU, FVS1-4, MVS, NNBSP) are stripped since
// users don't type them; unmapped codepoints pass through if ASCII, else are
// dropped. Total function. For the default keymap this inverts Convert exactly
// (apart from the stripped format chars and the "ng" digraph).
std::string Romanize(const std::wstring& text);

// True if `text` contains at least one traditional-Mongolian letter
// (U+1820..U+1842). Used to decide whether a romanized key is meaningful
// (placeholder / non-Mongolian text yields no romanized key).
bool ContainsMongolianLetter(const std::wstring& text);

}  // namespace paiza

#endif  // PAIZA_CORE_ROMANIZATION_H_
