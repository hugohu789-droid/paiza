// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid

#include "core/profile_switch.h"

#include <algorithm>

#include "core/external_engine_source.h"
#include "core/input_session.h"
#include "core/static_lexicon.h"
#include "core/user_dictionary.h"

namespace paiza {

void ProfileSwitch::Bind(std::string profile_id, ProfileBinding binding) {
  const auto existing =
      std::find_if(bindings_.begin(), bindings_.end(),
                   [&](const auto& entry) { return entry.first == profile_id; });
  if (existing != bindings_.end()) {
    existing->second = binding;
    return;
  }
  bindings_.emplace_back(std::move(profile_id), binding);
}

const ProfileBinding* ProfileSwitch::FindBinding(
    std::string_view profile_id) const {
  for (const auto& [id, binding] : bindings_) {
    if (id == profile_id) return &binding;
  }
  return nullptr;
}

bool ProfileSwitch::Activate(std::string_view profile_id) {
  const InputProfile* profile =
      registry_ != nullptr ? registry_->Find(profile_id) : nullptr;
  // An unknown id leaves everything as it was: a stale or mistyped settings
  // value must not half-switch the engine.
  if (profile == nullptr) return false;

  // Gate every bound source on whether it belongs to the profile being
  // activated. CandidateEngine::Query skips unavailable sources, so another
  // profile's entries can never appear in the candidate list.
  for (auto& [id, binding] : bindings_) {
    const bool active = id == profile->id;
    if (binding.user_dictionary != nullptr) {
      binding.user_dictionary->set_enabled(active);
    }
    if (binding.static_lexicon != nullptr) {
      binding.static_lexicon->set_enabled(active);
    }
    if (binding.engine_source != nullptr) {
      binding.engine_source->set_enabled(active);
    }
  }

  active_ = profile;
  active_id_ = profile->id;

  if (session_ != nullptr) ApplyTo(*session_);
  return true;
}

UserDictionary* ProfileSwitch::active_user_dictionary() const {
  if (active_ == nullptr) return nullptr;
  const ProfileBinding* binding = FindBinding(active_->id);
  return binding != nullptr ? binding->user_dictionary : nullptr;
}

StaticLexicon* ProfileSwitch::active_static_lexicon() const {
  if (active_ == nullptr) return nullptr;
  const ProfileBinding* binding = FindBinding(active_->id);
  return binding != nullptr ? binding->static_lexicon : nullptr;
}

void ProfileSwitch::ApplyTo(InputSession& session) const {
  if (active_ == nullptr) return;
  const ProfileBinding* binding = FindBinding(active_->id);
  // The session holds non-owning pointers; all three must be repointed
  // together or commits would be recorded into the previous profile's store.
  session.set_user_dictionary(
      binding != nullptr ? binding->user_dictionary : nullptr);
  session.set_static_lexicon(
      binding != nullptr ? binding->static_lexicon : nullptr);
  // Null here is already how InputSession means "no MVS insertion", so a
  // profile that disables the GB/T sequences needs no separate code path.
  session.set_fixed_sequences(
      binding != nullptr && active_->fixed_sequences ? binding->fixed_sequences
                                                     : nullptr);
}

}  // namespace paiza
