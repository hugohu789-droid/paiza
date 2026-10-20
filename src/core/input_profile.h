// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid
//
// Input profiles: the bundle of settings that distinguishes one Mongolian text
// encoding (and its candidate engine) from another, so that several can live in
// one build and be selected at runtime. See docs/input-profiles.md.
//
// A profile is data, not code: one plain text file per profile under
// data/profiles/, in the same "key value, '#' comments" style as
// equivalences.txt and fixed_sequences.txt. Adding an engine should be adding a
// file, not editing core code.
//
// Profiles are referenced by their string `id` (what the settings store keeps),
// never by an enum, so a new profile needs no change to any platform's settings
// schema.

#ifndef PAIZA_CORE_INPUT_PROFILE_H_
#define PAIZA_CORE_INPUT_PROFILE_H_

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace paiza {

// How much of the composition the host application shows inline. Profiles only
// supply the default; the user setting wins. See docs/input-profiles.md §5.
enum class InlineComposition {
  kFull = 0,     // Inline shows the converted text (Unicode default).
  kPlaceholder,  // Inline shows one placeholder character, keeping the
                 // composition range measurable for caret positioning.
  kHidden,       // Inline shows nothing.
};

// Parses "full" / "placeholder" / "hidden"; anything else yields `fallback`.
InlineComposition ParseInlineComposition(std::string_view text,
                                         InlineComposition fallback);
// Inverse of ParseInlineComposition.
std::string InlineCompositionName(InlineComposition value);
// Numeric round-trip for settings stores that keep an integer (Windows
// registry DWORD, NSUserDefaults). Out-of-range values yield kFull.
InlineComposition InlineCompositionFromValue(uint32_t value);

// Profile id used when settings carry none. Also the id of the built-in
// fallback below.
inline constexpr char kDefaultProfileId[] = "unicode";

struct InputProfile {
  // Stable key stored in settings and used by ProfileSwitch. Required.
  std::string id;
  // Shown in the settings dropdown. Defaults to `id` when the file omits it.
  std::wstring display_name;

  // File names, resolved by the caller against the deployment directory (they
  // are names rather than paths so a profile file stays machine-independent).
  std::wstring keymap;
  std::wstring lexicon;
  std::wstring user_db;
  // Candidate-window font family.
  std::wstring font;

  // Orthography and query behaviour. The defaults describe the Unicode
  // profile, so an omitted key keeps today's behaviour.
  bool fixed_sequences = true;  // GB/T D.2 MVS insertion + vowel harmony.
  bool romanize_query = true;   // The romanized second leg of the dual query.
  bool skeleton_input = true;   // Consonant-skeleton (vowel-optional) matching.
  // Latin keys treated as vowels by skeleton input; empty = use the built-in
  // default derived from the default layout.
  std::string vowel_keys;

  InlineComposition inline_default = InlineComposition::kFull;
};

// The built-in Unicode profile, identical to data/profiles/unicode.txt. Used
// when the profiles directory is missing or unreadable so the engine still
// runs with today's behaviour -- the same idea as KeyMapping::CreateDefault
// standing in for a missing keymap.txt. A test pins the two to each other.
InputProfile BuiltInUnicodeProfile();

class InputProfileRegistry {
 public:
  InputProfileRegistry() = default;

  // Parses one profile from `content` and appends it. A profile without an `id`
  // is rejected. Re-using an existing id replaces that profile. Returns true
  // when a profile was added or replaced.
  bool LoadFromString(const std::string& content);

  // Loads every *.txt in `dir`, sorted by file name so the dropdown order is
  // stable. Missing or unreadable directory leaves the registry untouched.
  // Returns the number of profiles successfully loaded.
  int LoadFromDirectory(const std::wstring& dir);

  // Null when no profile carries that id.
  const InputProfile* Find(std::string_view id) const;

  // Adds BuiltInUnicodeProfile() when the registry is empty, so callers always
  // have something to activate. No-op otherwise.
  void EnsureDefault();

  const std::vector<InputProfile>& All() const { return profiles_; }
  bool empty() const { return profiles_.empty(); }
  size_t size() const { return profiles_.size(); }
  void Clear() { profiles_.clear(); }

 private:
  std::vector<InputProfile> profiles_;
};

}  // namespace paiza

#endif  // PAIZA_CORE_INPUT_PROFILE_H_
