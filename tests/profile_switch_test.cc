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

}  // namespace
}  // namespace paiza
