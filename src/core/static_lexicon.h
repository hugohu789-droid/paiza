// Copyright 2026 Paiza. All rights reserved.
//
// Read-only static lexicon backed by the memory-mapped lexicon.bin. Startup is
// zero-parse: the file is mmap'd and validated (magic/version/endianness/CRC)
// once; a failed check leaves the source unloaded so the engine can fall back
// to the plain-text dictionary. Queries do exact + prefix lookups by binary
// search over the sorted key table, plus Damerau edit-distance<=1 fault
// tolerance, and rank results through the shared log-domain Scorer with an
// optional bigram context bonus.

#ifndef PAIZA_CORE_STATIC_LEXICON_H_
#define PAIZA_CORE_STATIC_LEXICON_H_

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "common/mapped_file.h"
#include "core/candidate_source.h"
#include "core/dawg.h"
#include "core/equivalence_table.h"
#include "core/lexicon_binary.h"
#include "core/levenshtein_automaton.h"
#include "core/scorer.h"

namespace paiza {

class StaticLexicon : public CandidateSource {
 public:
  StaticLexicon() = default;

  // Maps and validates the compiled lexicon. Returns false (unloaded) if the
  // file is missing, corrupt, wrong-version, or wrong-endian.
  bool Load(const std::wstring& bin_path);
  bool is_loaded() const { return header_ != nullptr; }

  // Optional tuning: scoring weights (key=value) and key-equivalence groups.
  // Missing files leave the built-in defaults (sensible weights, no
  // equivalences) untouched.
  void LoadTuning(const std::wstring& scoring_path,
                  const std::wstring& equivalences_path) {
    scorer_.LoadWeights(scoring_path);
    equivalences_.LoadFromFile(equivalences_path);
  }
  Scorer& scorer() { return scorer_; }
  EquivalenceTable& equivalences() { return equivalences_; }

  // CandidateSource:
  std::wstring name() const override { return L"lexicon"; }
  int priority() const override { return 100; }
  bool IsAvailable() const override { return is_loaded(); }
  std::vector<Candidate> Query(const std::string& latin_input,
                               size_t max_results) override;

  // Bigram context. Set the just-committed word so the next Query can reward
  // its likely successors; 0 clears the context. Atomic: on Windows the
  // commit notice and the next query arrive on different IPC worker threads.
  void SetContextWord(uint32_t word_id) {
    context_word_id_.store(word_id, std::memory_order_relaxed);
  }
  uint32_t context_word() const {
    return context_word_id_.load(std::memory_order_relaxed);
  }

  // Resolves a (Latin key, Mongolian text) pair to its word_id (0 = unknown).
  uint32_t WordIdOf(const std::string& latin,
                    const std::wstring& mongolian) const;

  // Zero-input prediction: the highest-weighted successors of context_word_id.
  std::vector<Candidate> PredictNext(uint32_t context_word_id,
                                     size_t max_results) const;

  uint32_t entry_count() const {
    return header_ ? header_->entry_count : 0;
  }
  uint32_t key_count() const { return header_ ? header_->key_count : 0; }
  uint32_t dawg_node_count() const {
    return header_ ? header_->dawg_node_count : 0;
  }

 private:
  // Views into the mapped file (valid while is_loaded()).
  const KeyRecord* keys() const;
  const char* key_blob() const;
  const EntryRecord* entries() const;
  const char* string_pool() const;
  const uint32_t* bigram_index() const;
  const BigramEdge* bigram_edges() const;
  const DawgNode* dawg_nodes() const;
  const DawgTrans* dawg_trans() const;

  // Byte comparison of the key at index `i` against `s`.
  int CompareKey(uint32_t i, const std::string& s) const;
  std::string KeyString(uint32_t i) const;
  std::wstring EntryText(uint32_t entry_index) const;

  // Index of the first key >= s (binary search over the sorted key table).
  uint32_t LowerBoundKey(const std::string& s) const;

  // Appends every entry under key index `key_i` to `out` with the given edit
  // cost, deduplicating by word_id via `seen`.
  void CollectKeyEntries(uint32_t key_i, int edit_cost,
                         const std::vector<uint32_t>& bigram_row,
                         std::vector<uint32_t>& seen_ids,
                         std::vector<Candidate>& out) const;

  // Damerau-Levenshtein<=1 tolerant search: a joint DFS over the DAWG and a
  // Levenshtein automaton (the Phase 3 "DFA x FST intersection"). `rank` is
  // the accumulated lexicographic rank, which equals the KeyRecord index when
  // an accepting state is reached.
  void IntersectDfs(uint32_t node_index, const LevenshteinAutomaton& automaton,
                    const LevenshteinAutomaton::State& state, uint32_t rank,
                    const std::vector<uint32_t>& bigram_row,
                    std::vector<uint32_t>& seen_ids,
                    std::vector<Candidate>& out) const;

  // Sorted list of successor word_ids for a context (empty if none).
  std::vector<uint32_t> BigramRow(uint32_t context_word_id) const;
  bool RowContains(const std::vector<uint32_t>& row, uint32_t word_id) const;

  MappedFile file_;
  const LexiconHeader* header_ = nullptr;
  DawgView dawg_;
  Scorer scorer_;
  EquivalenceTable equivalences_;
  std::atomic<uint32_t> context_word_id_{0};
};

}  // namespace paiza

#endif  // PAIZA_CORE_STATIC_LEXICON_H_
