// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid

#include "core/profile_switch.h"

#include <algorithm>

#include "core/external_engine_source.h"
#include "core/input_session.h"
#include "core/local_dictionary_source.h"
#include "core/static_lexicon.h"
#include "core/user_dictionary.h"

namespace paiza {

void ProfileSwitch::Bind(std::string profile_id, ProfileBinding binding) {
  ConnectUserWords(binding);
  const auto existing =
      std::find_if(bindings_.begin(), bindings_.end(),
                   [&](const auto& entry) { return entry.first == profile_id; });
  if (existing != bindings_.end()) {
    // The previous binding's dictionary may still be feeding commits to an
    // engine source that is about to be replaced.
    if (existing->second.user_dictionary != nullptr &&
        existing->second.user_dictionary != binding.user_dictionary) {
      existing->second.user_dictionary->set_commit_listener(nullptr);
    }
    existing->second = binding;
    return;
  }
  bindings_.emplace_back(std::move(profile_id), binding);
}

// A profile with both an engine and a user dictionary wants them joined:
// the dictionary stores, the engine ranks. Done here rather than in each
// front-end because there are three of them, and the one that forgot would
// be a profile whose habits silently did not stick.
void ProfileSwitch::ConnectUserWords(const ProfileBinding& binding) {
  UserDictionary* dictionary = binding.user_dictionary;
  ExternalEngineSource* engine = binding.engine_source;
  if (dictionary == nullptr) return;
  if (engine == nullptr) {
    dictionary->set_commit_listener(nullptr);
    dictionary->set_answers_queries(true);
    return;
  }
  engine->ReplayUserWords(dictionary->WordsByUse());
  // The engine took them, so it ranks them: this dictionary stores and
  // stops answering. Leaving it as a source as well would put every
  // committed word first for ever -- it sits above the engine -- and the
  // engine's habit model, which is the thing that lets an old habit fade,
  // would never show.
  dictionary->set_answers_queries(!engine->user_words_enabled());
  // Gating is checked inside: a commit recorded while another profile is
  // active still belongs to this profile's engine, and the engine is the
  // one that decides whether it wants user words at all.
  dictionary->set_commit_listener(
      [engine](const std::wstring& word, int64_t when) {
        engine->LearnCommitted(word, when);
      });
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
    if (binding.local_dictionary != nullptr) {
      binding.local_dictionary->set_enabled(active);
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

std::string ProfileSwitch::active_engine_error() const {
  if (active_ == nullptr) return std::string();
  // A profile that declares no engine has nothing to report, however the
  // binding looks.
  if (!active_->has_engine()) return std::string();
  const ProfileBinding* binding = FindBinding(active_->id);
  if (binding == nullptr) return std::string();
  // Never started: the reason was kept when the source could not be built.
  if (binding->engine_source == nullptr) return binding->engine_error;
  // Started, then failed. Being disabled is not a failure -- that is just
  // another profile being active -- so it is not reported.
  return binding->engine_source->failure();
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
