// Copyright 2026 Paiza. All rights reserved.
//
// Log-domain candidate scorer. Combines four signals into a single ranking
// score, per docs/core-engine-spec.md:
//
//   score = w_edit * (-edit_cost)
//         + w_bigram * (bigram_hit ? 1 : 0)
//         + w_sys  * ln(system_frequency + 1)
//         + w_user * ln(decayed_user_frequency + 1)
//
// The user-frequency term decays with a configurable half-life so a single
// mis-selection does not dominate forever. Weights load from scoring.txt
// (key=value) when present; otherwise sensible defaults apply. The scorer is
// stateless and reused across sources.

#ifndef PAIZA_CORE_SCORER_H_
#define PAIZA_CORE_SCORER_H_

#include <cstdint>
#include <string>

namespace paiza {

struct ScoringWeights {
  double w_edit = 6.0;    // Penalty per edit-distance unit (typo tolerance).
  double w_variant = 2.0; // Penalty per soft-equivalence substitution. Must be
                          // < w_edit (a variant beats a typo) and > 0 (an
                          // exact spelling beats its variant).
  double w_skeleton = 3.0;  // Penalty per vowel omitted in consonant-skeleton
                            // input, so full spellings outrank abbreviations.
  double w_phrase = 4.0;    // Penalty per extra word in a whole-phrase
                            // candidate, so a phrase is offered below the
                            // single word the user actually typed.
  double w_bigram = 2.0;  // Bonus when the previous word predicts this one.
  double w_sys = 1.0;     // Weight on ln(system frequency).
  double w_user = 1.2;    // Weight on ln(decayed user frequency).
  double user_halflife_days = 30.0;  // <= 0 disables decay.
};

struct ScoreSignals {
  uint32_t system_frequency = 0;
  uint32_t user_frequency = 0;
  int edit_cost = 0;      // 0 for exact/prefix/variant, 1 for typo matches.
  int variant_cost = 0;   // Count of soft-equivalence substitutions used
                          // (o/u, q/v, h/g, t/d): recalled but ranked below
                          // the exact spelling.
  int skeleton_cost = 0;  // Vowels omitted in consonant-skeleton input.
  int phrase_cost = 0;    // Extra words in a whole-phrase candidate (words-1).
  bool bigram_hit = false;
  int64_t last_used_unix = 0;  // 0 = never used (no decay applied).
};

class Scorer {
 public:
  Scorer() = default;
  explicit Scorer(const ScoringWeights& weights) : weights_(weights) {}

  // Loads weights from a key=value file. Missing file -> defaults, missing
  // keys keep their default. Never throws.
  void LoadWeights(const std::wstring& path);

  const ScoringWeights& weights() const { return weights_; }

  // Raw log-domain score (higher is better). now_unix is the current time in
  // seconds for the recency decay; pass 0 to skip decay entirely.
  double Score(const ScoreSignals& signals, int64_t now_unix) const;

  // Convenience: score scaled to an int32 suitable for Candidate::score.
  int32_t ScoreToInt(const ScoreSignals& signals, int64_t now_unix) const;

 private:
  ScoringWeights weights_;
};

}  // namespace paiza

#endif  // PAIZA_CORE_SCORER_H_
