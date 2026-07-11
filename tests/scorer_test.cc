// Copyright 2026 Paiza. All rights reserved.

#include "core/scorer.h"

#include <gtest/gtest.h>

#include <cmath>

namespace paiza {
namespace {

TEST(ScorerTest, HigherFrequencyScoresHigher) {
  Scorer scorer;
  ScoreSignals low;
  low.system_frequency = 10;
  ScoreSignals high;
  high.system_frequency = 1000;
  EXPECT_GT(scorer.Score(high, 0), scorer.Score(low, 0));
}

TEST(ScorerTest, EditCostPenalisesTolerantMatches) {
  Scorer scorer;
  ScoreSignals exact;
  exact.system_frequency = 100;
  exact.edit_cost = 0;
  ScoreSignals tolerant;
  tolerant.system_frequency = 100;
  tolerant.edit_cost = 1;
  EXPECT_GT(scorer.Score(exact, 0), scorer.Score(tolerant, 0));
  // The edit penalty (w_edit=6) offsets roughly a 400x frequency gap, so an
  // exact but moderately rarer word still beats a more common misspelling.
  ScoreSignals rare_exact;
  rare_exact.system_frequency = 100;
  rare_exact.edit_cost = 0;
  ScoreSignals common_tolerant;
  common_tolerant.system_frequency = 1000;  // 10x more common, still loses.
  common_tolerant.edit_cost = 1;
  EXPECT_GT(scorer.Score(rare_exact, 0), scorer.Score(common_tolerant, 0));
}

TEST(ScorerTest, BigramHitAddsBonus) {
  Scorer scorer;
  ScoreSignals no_hit;
  no_hit.system_frequency = 50;
  ScoreSignals hit = no_hit;
  hit.bigram_hit = true;
  EXPECT_GT(scorer.Score(hit, 0), scorer.Score(no_hit, 0));
}

TEST(ScorerTest, UserFrequencyDecaysWithAge) {
  Scorer scorer;
  const int64_t now = 1'000'000'000;
  ScoreSignals fresh;
  fresh.user_frequency = 100;
  fresh.last_used_unix = now;  // Used just now.
  ScoreSignals stale;
  stale.user_frequency = 100;
  stale.last_used_unix = now - 60LL * 86400;  // Two half-lives ago (~30d each).
  EXPECT_GT(scorer.Score(fresh, now), scorer.Score(stale, now));
}

TEST(ScorerTest, LoadWeightsOverridesDefaults) {
  Scorer scorer;
  const std::string content = "w_sys=0\nw_bigram=99\n";
  // Write to a temp file and load.
  const std::string path = ::testing::TempDir() + "paiza_scoring.txt";
  {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    ASSERT_NE(f, nullptr);
    std::fwrite(content.data(), 1, content.size(), f);
    std::fclose(f);
  }
  scorer.LoadWeights(std::wstring(path.begin(), path.end()));
  EXPECT_DOUBLE_EQ(scorer.weights().w_sys, 0.0);
  EXPECT_DOUBLE_EQ(scorer.weights().w_bigram, 99.0);
  // Untouched keys keep their defaults.
  EXPECT_DOUBLE_EQ(scorer.weights().w_edit, 6.0);
}

}  // namespace
}  // namespace paiza
