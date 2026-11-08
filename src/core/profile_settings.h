// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid
//
// Settings the user may choose separately per input profile.
//
// The candidate font is the first of them, and it has to be per profile
// because it is not only a matter of taste: a ZCode profile's text is
// legible solely in a form-code font, and a Unicode Mongolian font renders
// the very same code points as the Sibe and Manchu letters they are in
// Unicode — different words, not a different look. One global font setting
// therefore cannot serve both profiles.
//
// The values are packed into the single string each platform's settings
// store already keeps, rather than adding a key per profile to three stores
// (registry, text file, NSUserDefaults):
//
//     zcode=Z52 Hara Tig;unicode=Noto Sans Mongolian
//
// A string with no '=' is a plain value from before this existed and is
// read as the value for every profile, so an existing setting keeps working
// and is migrated the first time the user changes it. A profile id cannot
// contain '=' or ';' (they are short slugs); a value containing ';' cannot
// be stored, which is checked rather than assumed.

#ifndef PAIZA_CORE_PROFILE_SETTINGS_H_
#define PAIZA_CORE_PROFILE_SETTINGS_H_

#include <string>
#include <string_view>

namespace paiza {

// The value stored for `profile_id`, or an empty string when the packed
// string holds none for it. A legacy unpacked string answers for every id.
std::wstring PerProfileValue(std::wstring_view packed,
                             std::string_view profile_id);

// `packed` with `profile_id`'s value set, leaving the other profiles'
// values alone and keeping them in a stable order. An empty `value` removes
// the entry, so the profile falls back to its declared default again.
// Returns `packed` unchanged when the value cannot be represented.
std::wstring SetPerProfileValue(std::wstring_view packed,
                                std::string_view profile_id,
                                std::wstring_view value);

// True when `value` can be stored (no ';', which is the separator).
bool IsStorablePerProfileValue(std::wstring_view value);

}  // namespace paiza

#endif  // PAIZA_CORE_PROFILE_SETTINGS_H_
