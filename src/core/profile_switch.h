// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid
//
// Applies an input profile at runtime: enables that profile's candidate
// sources, disables every other profile's, and repoints the session's
// non-owning collaborators. See docs/input-profiles.md.
//
// This exists so the three platform front-ends do not each repeat an N-way
// switch over profiles: their ApplySettings calls Activate() and nothing else.
// Being platform-neutral it is also unit-testable without a display, which is
// how profile switching is verified rather than by hand on three systems.
//
// Sources cannot be added or removed after startup (CandidateEngine::AddSource
// is startup-only and not thread-safe against Query), so every profile's
// sources are registered up front and gated by availability instead.

#ifndef PAIZA_CORE_PROFILE_SWITCH_H_
#define PAIZA_CORE_PROFILE_SWITCH_H_

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/input_profile.h"

namespace paiza {

class FixedSequenceTable;
class InputSession;
class StaticLexicon;
class UserDictionary;

// The engine objects belonging to one profile. Any member may be null; a null
// member simply means the profile has nothing of that kind. All pointed-to
// objects must outlive the ProfileSwitch.
struct ProfileBinding {
  UserDictionary* user_dictionary = nullptr;
  StaticLexicon* static_lexicon = nullptr;
  // Compose-side GB/T suffix table. Honoured only when the profile's
  // `fixed_sequences` flag is set, so a non-Unicode profile gets no MVS
  // insertion even if a table is bound.
  const FixedSequenceTable* fixed_sequences = nullptr;
};

class ProfileSwitch {
 public:
  // `registry` must outlive the switch. `session` may be null when only source
  // gating is wanted; when present it is repointed on every Activate.
  ProfileSwitch(const InputProfileRegistry* registry, InputSession* session)
      : registry_(registry), session_(session) {}

  ProfileSwitch(const ProfileSwitch&) = delete;
  ProfileSwitch& operator=(const ProfileSwitch&) = delete;

  // Associates engine objects with a profile id. Binding an id again replaces
  // the previous binding. Binding does not activate.
  void Bind(std::string profile_id, ProfileBinding binding);

  // Makes `profile_id` current: gates every bound source on whether it belongs
  // to that profile, then repoints the session. Returns false and changes
  // nothing when the id is not in the registry, so a stale settings value can
  // never leave the engine in a half-switched state.
  bool Activate(std::string_view profile_id);

  // Applies the active profile's bindings to `session`. Activate() already
  // does this for the session given to the constructor; this is for hosts
  // that create sessions later or create several (macOS makes one per input
  // client). No-op before the first successful Activate.
  void ApplyTo(InputSession& session) const;

  // The active profile, or null before a successful Activate.
  const InputProfile* active() const { return active_; }
  std::string_view active_id() const { return active_id_; }

 private:
  const ProfileBinding* FindBinding(std::string_view profile_id) const;

  const InputProfileRegistry* registry_;
  InputSession* session_;
  std::vector<std::pair<std::string, ProfileBinding>> bindings_;
  const InputProfile* active_ = nullptr;
  std::string active_id_;
};

}  // namespace paiza

#endif  // PAIZA_CORE_PROFILE_SWITCH_H_
