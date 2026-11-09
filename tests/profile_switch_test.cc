// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid
//
// ProfileSwitch is how a front-end moves between input profiles, so these
// tests pin the two things that must never go wrong: a profile's entries can
// never leak into another profile's candidate list, and the session's
// non-owning collaborators are repointed together.

#include "core/profile_switch.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>

#include "core/candidate_engine.h"
#include "core/fixed_sequences.h"
#include "core/input_session.h"
#include "core/key_mapping_store.h"
#include "core/local_dictionary_source.h"
#include "core/static_lexicon.h"
#include "core/user_dictionary.h"

namespace paiza {
namespace {

// The conversion of "abdvr" under the default key mapping, with and without
// the GB/T D.2 compose-side MVS insertion. Both verified against the shipped
// engine via tools/paiza_query rather than written from expectation.
constexpr wchar_t kAbdvrWithMvs[] =
    L"\x1820\x182A\x180E\x1833\x1824\x1837";  // MVS at U+180E.
constexpr wchar_t kAbdvrRaw[] = L"\x1820\x182A\x1833\x1824\x1837";

std::string ReadRepoFile(const char* relative) {
  const std::filesystem::path path =
      std::filesystem::path(PAIZA_SOURCE_DIR) / relative;
  std::ifstream stream(path, std::ios::binary);
  std::ostringstream buffer;
  buffer << stream.rdbuf();
  return buffer.str();
}

class ProfileSwitchTest : public ::testing::Test {
 protected:
  ProfileSwitchTest() : mappings_(L"Z:\\nonexistent\\keymap.txt") {
    // Two profiles that differ exactly where it matters: one applies the GB/T
    // fixed sequences, the other does not (as a PUA profile would not).
    registry_.LoadFromString("id uni\nfixed_sequences yes\n");
    registry_.LoadFromString("id zc\nfixed_sequences no\n");

    fixed_.LoadFromString(ReadRepoFile("data/fixed_sequences.txt"));

    auto uni = std::make_unique<UserDictionary>(L"");  // Memory-only.
    auto zc = std::make_unique<UserDictionary>(L"");
    uni_dict_ = uni.get();
    zc_dict_ = zc.get();
    uni_dict_->RecordCommit("shared", L"\x1820");  // Unicode Mongolian A.
    zc_dict_->RecordCommit("shared", L"\xE001");   // A private-use shape code.
    engine_.AddSource(std::move(uni));
    engine_.AddSource(std::move(zc));
  }

  // Binds both profiles with their own dictionary; only "uni" gets the table.
  void BindBoth(ProfileSwitch& sw) {
    ProfileBinding uni;
    uni.user_dictionary = uni_dict_;
    uni.fixed_sequences = &fixed_;
    sw.Bind("uni", uni);

    ProfileBinding zc;
    zc.user_dictionary = zc_dict_;
    zc.fixed_sequences = &fixed_;  // Bound, but the profile disables it.
    sw.Bind("zc", zc);
  }

  std::wstring PreeditOf(InputSession& session, const char* keys) {
    for (const char* p = keys; *p != '\0'; ++p) session.HandleLetter(*p);
    return session.preedit();
  }

  InputProfileRegistry registry_;
  CandidateEngine engine_;
  KeyMappingStore mappings_;
  FixedSequenceTable fixed_;
  UserDictionary* uni_dict_ = nullptr;
  UserDictionary* zc_dict_ = nullptr;
};

TEST_F(ProfileSwitchTest, ActivateUnknownIdChangesNothing) {
  ProfileSwitch sw(&registry_, nullptr);
  BindBoth(sw);
  ASSERT_TRUE(sw.Activate("uni"));

  EXPECT_FALSE(sw.Activate("no_such_profile"));
  // Still the previously active profile, not half-switched.
  EXPECT_EQ(sw.active_id(), "uni");
  EXPECT_TRUE(uni_dict_->enabled());
  EXPECT_FALSE(zc_dict_->enabled());
}

TEST_F(ProfileSwitchTest, OnlyActiveProfileContributesCandidates) {
  ProfileSwitch sw(&registry_, nullptr);
  BindBoth(sw);

  ASSERT_TRUE(sw.Activate("uni"));
  auto candidates = engine_.Query("shared", 9);
  ASSERT_EQ(candidates.size(), 1u);
  EXPECT_EQ(candidates[0].text, L"\x1820");

  ASSERT_TRUE(sw.Activate("zc"));
  candidates = engine_.Query("shared", 9);
  ASSERT_EQ(candidates.size(), 1u);
  EXPECT_EQ(candidates[0].text, L"\xE001");
}

TEST_F(ProfileSwitchTest, UnboundProfileDisablesEveryBoundSource) {
  registry_.LoadFromString("id third\n");
  ProfileSwitch sw(&registry_, nullptr);
  BindBoth(sw);

  ASSERT_TRUE(sw.Activate("third"));  // Registered but has no binding.
  EXPECT_FALSE(uni_dict_->enabled());
  EXPECT_FALSE(zc_dict_->enabled());
  EXPECT_TRUE(engine_.Query("shared", 9).empty());
}

TEST_F(ProfileSwitchTest, ProfileWithFixedSequencesInsertsMvs) {
  InputSession session(&engine_, &mappings_);
  ProfileSwitch sw(&registry_, &session);
  BindBoth(sw);

  ASSERT_TRUE(sw.Activate("uni"));
  EXPECT_EQ(PreeditOf(session, "abdvr"), kAbdvrWithMvs);
}

// The same table is bound, but the profile disables it: no MVS insertion.
TEST_F(ProfileSwitchTest, ProfileWithoutFixedSequencesLeavesRawConversion) {
  InputSession session(&engine_, &mappings_);
  ProfileSwitch sw(&registry_, &session);
  BindBoth(sw);

  ASSERT_TRUE(sw.Activate("zc"));
  EXPECT_EQ(PreeditOf(session, "abdvr"), kAbdvrRaw);
}

TEST_F(ProfileSwitchTest, SwitchingProfilesChangesComposeBehaviour) {
  InputSession session(&engine_, &mappings_);
  ProfileSwitch sw(&registry_, &session);
  BindBoth(sw);

  ASSERT_TRUE(sw.Activate("uni"));
  EXPECT_EQ(PreeditOf(session, "abdvr"), kAbdvrWithMvs);

  session.Reset();
  ASSERT_TRUE(sw.Activate("zc"));
  EXPECT_EQ(PreeditOf(session, "abdvr"), kAbdvrRaw);

  session.Reset();
  ASSERT_TRUE(sw.Activate("uni"));
  EXPECT_EQ(PreeditOf(session, "abdvr"), kAbdvrWithMvs);
}

TEST_F(ProfileSwitchTest, BindReplacesPreviousBindingForSameId) {
  InputSession session(&engine_, &mappings_);
  ProfileSwitch sw(&registry_, &session);
  BindBoth(sw);

  ProfileBinding without_table;
  without_table.user_dictionary = uni_dict_;  // No fixed_sequences this time.
  sw.Bind("uni", without_table);

  ASSERT_TRUE(sw.Activate("uni"));
  EXPECT_EQ(PreeditOf(session, "abdvr"), kAbdvrRaw);
}

TEST_F(ProfileSwitchTest, WorksWithoutASession) {
  ProfileSwitch sw(&registry_, nullptr);  // Source gating only.
  BindBoth(sw);
  EXPECT_TRUE(sw.Activate("zc"));
  EXPECT_FALSE(uni_dict_->enabled());
  EXPECT_TRUE(zc_dict_->enabled());
}

TEST_F(ProfileSwitchTest, ActiveAccessorsReflectLastActivation) {
  ProfileSwitch sw(&registry_, nullptr);
  BindBoth(sw);
  EXPECT_EQ(sw.active(), nullptr);  // Nothing activated yet.

  ASSERT_TRUE(sw.Activate("zc"));
  ASSERT_NE(sw.active(), nullptr);
  EXPECT_EQ(sw.active()->id, "zc");
  EXPECT_FALSE(sw.active()->fixed_sequences);
  EXPECT_EQ(sw.active_id(), "zc");
}

TEST_F(ProfileSwitchTest, ActivateWithoutRegistryEntryIsRejected) {
  InputProfileRegistry empty;
  ProfileSwitch sw(&empty, nullptr);
  BindBoth(sw);
  EXPECT_FALSE(sw.Activate("uni"));
  EXPECT_EQ(sw.active(), nullptr);
}

// Hosts that create sessions after activation (macOS makes one per input
// client) wire them through ApplyTo rather than the constructor.
TEST_F(ProfileSwitchTest, ApplyToWiresALateCreatedSession) {
  ProfileSwitch sw(&registry_, nullptr);
  BindBoth(sw);
  ASSERT_TRUE(sw.Activate("uni"));

  InputSession late(&engine_, &mappings_);
  sw.ApplyTo(late);
  EXPECT_EQ(PreeditOf(late, "abdvr"), kAbdvrWithMvs);

  ASSERT_TRUE(sw.Activate("zc"));
  InputSession later(&engine_, &mappings_);
  sw.ApplyTo(later);
  EXPECT_EQ(PreeditOf(later, "abdvr"), kAbdvrRaw);
}

TEST_F(ProfileSwitchTest, ApplyToBeforeActivateIsANoOp) {
  ProfileSwitch sw(&registry_, nullptr);
  BindBoth(sw);
  InputSession session(&engine_, &mappings_);
  sw.ApplyTo(session);  // Nothing activated yet: must not crash or wire.
  EXPECT_EQ(PreeditOf(session, "abdvr"), kAbdvrRaw);
}

// Callers that outlive a profile change must resolve the active store
// through the switch; a cached pointer would write into the wrong profile.
TEST_F(ProfileSwitchTest, ActiveAccessorsFollowTheProfile) {
  ProfileSwitch sw(&registry_, nullptr);
  BindBoth(sw);
  EXPECT_EQ(sw.active_user_dictionary(), nullptr);  // Before activation.

  ASSERT_TRUE(sw.Activate("uni"));
  EXPECT_EQ(sw.active_user_dictionary(), uni_dict_);

  ASSERT_TRUE(sw.Activate("zc"));
  EXPECT_EQ(sw.active_user_dictionary(), zc_dict_);
}

TEST_F(ProfileSwitchTest, ActiveAccessorsNullForUnboundProfile) {
  registry_.LoadFromString("id third\n");
  ProfileSwitch sw(&registry_, nullptr);
  BindBoth(sw);
  ASSERT_TRUE(sw.Activate("third"));
  EXPECT_EQ(sw.active_user_dictionary(), nullptr);
  EXPECT_EQ(sw.active_static_lexicon(), nullptr);
}

// The plain-text dictionary holds Unicode Mongolian words. Before it was
// gated it answered for every profile, so a form-code profile's candidate
// list was a mixture -- and because a form-code font has no glyphs for
// Unicode Mongolian, the host fell back per character and part of the list
// rendered in a different font, which looks like the font being ignored
// rather than like the wrong source answering.
TEST_F(ProfileSwitchTest, LocalDictionaryAnswersOnlyForItsOwnProfile) {
  LocalDictionarySource dictionary;
  dictionary.AddEntry("shared", L"\x1830\x1820\x1828", 100);
  ASSERT_TRUE(dictionary.IsAvailable());

  ProfileSwitch sw(&registry_, nullptr);
  ProfileBinding uni;
  uni.local_dictionary = &dictionary;
  sw.Bind("uni", uni);
  sw.Bind("zc", ProfileBinding());

  ASSERT_TRUE(sw.Activate("uni"));
  EXPECT_TRUE(dictionary.enabled());
  EXPECT_TRUE(dictionary.IsAvailable());

  ASSERT_TRUE(sw.Activate("zc"));
  EXPECT_FALSE(dictionary.enabled());
  EXPECT_FALSE(dictionary.IsAvailable());
  EXPECT_TRUE(dictionary.Query("shared", 9).empty() ||
              !dictionary.IsAvailable());

  // Gated off, not broken: switching back restores it.
  ASSERT_TRUE(sw.Activate("uni"));
  EXPECT_TRUE(dictionary.IsAvailable());
}

// An empty dictionary is unavailable whether or not it is enabled, so the
// gate must not make a loaded-but-inactive one look loaded.
TEST_F(ProfileSwitchTest, LocalDictionaryStaysUnavailableWhenEmpty) {
  LocalDictionarySource empty;
  ProfileSwitch sw(&registry_, nullptr);
  ProfileBinding uni;
  uni.local_dictionary = &empty;
  sw.Bind("uni", uni);
  ASSERT_TRUE(sw.Activate("uni"));
  EXPECT_TRUE(empty.enabled());
  EXPECT_FALSE(empty.IsAvailable());
}

TEST_F(ProfileSwitchTest, RegistryAccessorReturnsTheRegistry) {
  ProfileSwitch sw(&registry_, nullptr);
  EXPECT_EQ(sw.registry(), &registry_);
}

// ---- behaviour fields a non-Unicode profile needs ----------------------
//
// Four profile fields used to be parsed and then ignored, so a profile could
// state them correctly and nothing happened. These pin that each one now
// reaches the thing that acts on it.

TEST_F(ProfileSwitchTest, RomanizeQueryFollowsTheProfile) {
  InputProfileRegistry registry;
  ASSERT_TRUE(registry.LoadFromString("id yes\nromanize_query yes\n"));
  ASSERT_TRUE(registry.LoadFromString("id no\nromanize_query no\n"));
  InputSession session(&engine_, &mappings_);
  ProfileSwitch sw(&registry, &session);
  sw.Bind("yes", ProfileBinding());
  sw.Bind("no", ProfileBinding());

  ASSERT_TRUE(sw.Activate("yes"));
  EXPECT_TRUE(session.romanize_query());
  ASSERT_TRUE(sw.Activate("no"));
  EXPECT_FALSE(session.romanize_query())
      << "a profile whose preedit is not Unicode Mongolian must be able to "
         "switch off the second, romanized query key";
  ASSERT_TRUE(sw.Activate("yes"));
  EXPECT_TRUE(session.romanize_query()) << "and switching back restores it";
}

TEST_F(ProfileSwitchTest, RomanizeQueryDefaultsOnForAProfileThatIsSilent) {
  InputProfileRegistry registry;
  ASSERT_TRUE(registry.LoadFromString("id q\n"));
  InputSession session(&engine_, &mappings_);
  ProfileSwitch sw(&registry, &session);
  sw.Bind("q", ProfileBinding());
  ASSERT_TRUE(sw.Activate("q"));
  EXPECT_TRUE(session.romanize_query());
}

TEST_F(ProfileSwitchTest, SkeletonInputAndVowelKeysReachTheLexicon) {
  InputProfileRegistry registry;
  ASSERT_TRUE(registry.LoadFromString("id on\nskeleton_input yes\n"));
  ASSERT_TRUE(registry.LoadFromString(
      "id off\nskeleton_input no\nvowel_keys xyz\n"));
  StaticLexicon lexicon;  // Not loaded; only the settings matter here.
  ProfileSwitch sw(&registry, /*session=*/nullptr);
  ProfileBinding binding;
  binding.static_lexicon = &lexicon;
  sw.Bind("on", binding);
  sw.Bind("off", binding);

  ASSERT_TRUE(sw.Activate("on"));
  EXPECT_TRUE(lexicon.skeleton_input());
  ASSERT_TRUE(sw.Activate("off"));
  EXPECT_FALSE(lexicon.skeleton_input())
      << "consonant-skeleton input is a convention of one orthography, not a "
         "property of lexicon lookup";
}

// The setting is only a *default*: it must not overrule a user who chose.
TEST_F(ProfileSwitchTest, InlineCompositionPrefersTheUsersChoice) {
  InputProfileRegistry registry;
  ASSERT_TRUE(registry.LoadFromString("id full\ninline_default full\n"));
  ASSERT_TRUE(registry.LoadFromString("id hidden\ninline_default hidden\n"));
  ProfileSwitch sw(&registry, /*session=*/nullptr);
  sw.Bind("full", ProfileBinding());
  sw.Bind("hidden", ProfileBinding());

  ASSERT_TRUE(sw.Activate("hidden"));
  EXPECT_EQ(sw.active_inline_composition(/*user_set=*/false,
                                         InlineComposition::kFull),
            InlineComposition::kHidden)
      << "no choice of the user's, so the profile's default";
  EXPECT_EQ(sw.active_inline_composition(/*user_set=*/true,
                                         InlineComposition::kFull),
            InlineComposition::kFull)
      << "the user chose, so the user wins";

  ASSERT_TRUE(sw.Activate("full"));
  EXPECT_EQ(sw.active_inline_composition(/*user_set=*/false,
                                         InlineComposition::kHidden),
            InlineComposition::kFull);
}

TEST_F(ProfileSwitchTest, InlineCompositionWithoutAnActiveProfile) {
  InputProfileRegistry registry;
  ProfileSwitch sw(&registry, /*session=*/nullptr);
  EXPECT_EQ(sw.active_inline_composition(/*user_set=*/false,
                                         InlineComposition::kHidden),
            InlineComposition::kFull);
  EXPECT_EQ(sw.active_inline_composition(/*user_set=*/true,
                                         InlineComposition::kHidden),
            InlineComposition::kHidden);
}

// The shipped ZCode profile is the reason these exist; pin what it asks for.
TEST_F(ProfileSwitchTest, TheShippedZCodeProfileGetsWhatItDeclares) {
  InputProfileRegistry registry;
  const std::filesystem::path dir =
      std::filesystem::path(PAIZA_SOURCE_DIR) / "data" / "profiles";
  ASSERT_GE(registry.LoadFromDirectory(dir.wstring()), 2);
  StaticLexicon lexicon;
  InputSession session(&engine_, &mappings_);
  ProfileSwitch sw(&registry, &session);
  ProfileBinding binding;
  binding.static_lexicon = &lexicon;
  sw.Bind("zcode", binding);
  sw.Bind("unicode", binding);

  ASSERT_TRUE(sw.Activate("zcode"));
  EXPECT_FALSE(session.romanize_query());
  EXPECT_FALSE(lexicon.skeleton_input());
  EXPECT_EQ(sw.active_inline_composition(/*user_set=*/false,
                                         InlineComposition::kFull),
            InlineComposition::kHidden);

  ASSERT_TRUE(sw.Activate("unicode"));
  EXPECT_TRUE(session.romanize_query());
  EXPECT_TRUE(lexicon.skeleton_input());
  EXPECT_EQ(sw.active_inline_composition(/*user_set=*/false,
                                         InlineComposition::kHidden),
            InlineComposition::kFull);
}

// A font the user chose before the setting was per profile applies to the
// profile they chose it for -- which can only have been the default one,
// since it is the only profile that existed. It must NOT override the font
// another profile declares: ZCode in a Unicode Mongolian font renders as
// the Sibe and Manchu letters those code points are in Unicode, so the
// wrong font there is wrong *text*, not a different look.
TEST_F(ProfileSwitchTest, ALegacyFontDoesNotOverrideWhatAProfileDeclares) {
  InputProfileRegistry registry;
  ASSERT_TRUE(
      registry.LoadFromString("id unicode\nfont Noto Sans Mongolian\n"));
  ASSERT_TRUE(registry.LoadFromString("id zcode\nfont Z52 Hara Tig\n"));
  ProfileSwitch sw(&registry, /*session=*/nullptr);
  sw.Bind("unicode", ProfileBinding());
  sw.Bind("zcode", ProfileBinding());

  // What a fresh install has: one unpacked value, no profile ids in it.
  const std::wstring legacy = L"Noto Sans Mongolian";

  ASSERT_TRUE(sw.Activate("unicode"));
  EXPECT_EQ(sw.active_candidate_font(legacy, L"Fallback"),
            L"Noto Sans Mongolian");

  ASSERT_TRUE(sw.Activate("zcode"));
  EXPECT_EQ(sw.active_candidate_font(legacy, L"Fallback"), L"Z52 Hara Tig")
      << "the profile's own font must win over a value from before the "
         "setting was per profile";
}

// And an explicit per-profile choice still beats the declaration.
TEST_F(ProfileSwitchTest, AnExplicitChoiceBeatsTheProfilesDeclaration) {
  InputProfileRegistry registry;
  ASSERT_TRUE(registry.LoadFromString("id zcode\nfont Z52 Hara Tig\n"));
  ProfileSwitch sw(&registry, /*session=*/nullptr);
  sw.Bind("zcode", ProfileBinding());
  ASSERT_TRUE(sw.Activate("zcode"));
  EXPECT_EQ(sw.active_candidate_font(L"zcode=Z52 Galt Tig", L"Fallback"),
            L"Z52 Galt Tig");
}

}  // namespace
}  // namespace paiza
