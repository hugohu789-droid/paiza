// Copyright 2026 Paiza. All rights reserved.

#include "core/static_lexicon.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <system_error>

#include "common/logging.h"
#include "common/string_utils.h"
#include "core/equivalence_table.h"
#include "core/scorer.h"

namespace paiza {

void StaticLexicon::LoadTuning(const std::wstring& scoring_path,
                              const std::wstring& equivalences_path) {
  scorer_.LoadWeights(scoring_path);
  // The lexicon already carries its equivalence set; only override it when the
  // user actually placed a file, so a missing path doesn't wipe the embedded
  // rules.
  std::error_code ec;
  if (!equivalences_path.empty() &&
      std::filesystem::exists(std::filesystem::path(equivalences_path), ec)) {
    equivalences_.LoadFromFile(equivalences_path);
  }
}

bool StaticLexicon::Load(const std::wstring& bin_path) {
  file_.Close();
  header_ = nullptr;
  context_word_id_.store(0, std::memory_order_relaxed);
  if (!file_.Open(bin_path)) return false;

  if (file_.size() < sizeof(LexiconHeader)) {
    file_.Close();
    return false;
  }
  const auto* header = reinterpret_cast<const LexiconHeader*>(file_.data());
  if (header->magic != kLexiconMagic || header->version != kLexiconVersion ||
      header->endian_mark != kLexiconEndianMark ||
      header->file_size != file_.size()) {
    PAIZA_LOG_WARN(L"Lexicon header invalid; ignoring compiled lexicon.");
    file_.Close();
    return false;
  }
  // Validate the payload checksum; reject a corrupt/truncated file wholesale.
  const uint8_t* payload = file_.data() + sizeof(LexiconHeader);
  const size_t payload_size = file_.size() - sizeof(LexiconHeader);
  if (Crc32(payload, payload_size) != header->payload_crc32) {
    PAIZA_LOG_WARN(L"Lexicon CRC mismatch; ignoring compiled lexicon.");
    file_.Close();
    return false;
  }
  header_ = header;
  if (header_->dawg_node_count > 0) {
    dawg_ = DawgView(dawg_nodes(), header_->dawg_node_count, dawg_trans(),
                     header_->dawg_trans_count, header_->dawg_root);
  } else {
    dawg_ = DawgView();
  }
  // Soft-equivalence rules travel inside the lexicon so compile and query
  // time use the same set (a later LoadTuning file can still override).
  if (header_->equiv_blob_size > 0) {
    const char* blob = reinterpret_cast<const char*>(file_.data() +
                                                     header_->equiv_blob_offset);
    equivalences_.LoadFromString(std::string(blob, header_->equiv_blob_size));
  }
  Logf(LogLevel::kInfo, L"Static lexicon mapped: %u entries",
       header_->entry_count);
  return true;
}

const KeyRecord* StaticLexicon::keys() const {
  return reinterpret_cast<const KeyRecord*>(file_.data() +
                                            header_->keys_offset);
}
const char* StaticLexicon::key_blob() const {
  return reinterpret_cast<const char*>(file_.data() +
                                       header_->key_blob_offset);
}
const EntryRecord* StaticLexicon::entries() const {
  return reinterpret_cast<const EntryRecord*>(file_.data() +
                                              header_->entries_offset);
}
const char* StaticLexicon::string_pool() const {
  return reinterpret_cast<const char*>(file_.data() +
                                       header_->string_pool_offset);
}
const uint32_t* StaticLexicon::bigram_index() const {
  return reinterpret_cast<const uint32_t*>(file_.data() +
                                           header_->bigram_index_offset);
}
const BigramEdge* StaticLexicon::bigram_edges() const {
  return reinterpret_cast<const BigramEdge*>(file_.data() +
                                             header_->bigram_edges_offset);
}
const DawgNode* StaticLexicon::dawg_nodes() const {
  return reinterpret_cast<const DawgNode*>(file_.data() +
                                           header_->dawg_nodes_offset);
}
const DawgTrans* StaticLexicon::dawg_trans() const {
  return reinterpret_cast<const DawgTrans*>(file_.data() +
                                            header_->dawg_trans_offset);
}

std::string StaticLexicon::KeyString(uint32_t i) const {
  const KeyRecord& k = keys()[i];
  return std::string(key_blob() + k.key_offset, k.key_length);
}

int StaticLexicon::CompareKey(uint32_t i, const std::string& s) const {
  const KeyRecord& k = keys()[i];
  const size_t n = std::min<size_t>(k.key_length, s.size());
  const int cmp = std::memcmp(key_blob() + k.key_offset, s.data(), n);
  if (cmp != 0) return cmp;
  if (k.key_length < s.size()) return -1;
  if (k.key_length > s.size()) return 1;
  return 0;
}

std::wstring StaticLexicon::EntryText(uint32_t entry_index) const {
  const EntryRecord& e = entries()[entry_index];
  return Utf8ToWide(
      std::string(string_pool() + e.text_offset, e.text_length));
}

uint32_t StaticLexicon::LowerBoundKey(const std::string& s) const {
  uint32_t lo = 0, hi = header_->key_count;
  while (lo < hi) {
    const uint32_t mid = lo + (hi - lo) / 2;
    if (CompareKey(mid, s) < 0)
      lo = mid + 1;
    else
      hi = mid;
  }
  return lo;
}

std::vector<uint32_t> StaticLexicon::BigramRow(uint32_t context_word_id) const {
  std::vector<uint32_t> row;
  if (header_->bigram_row_count == 0 || context_word_id == 0 ||
      context_word_id > header_->bigram_row_count) {
    return row;
  }
  const uint32_t r = context_word_id - 1;
  const uint32_t begin = bigram_index()[r];
  const uint32_t end = bigram_index()[r + 1];
  const BigramEdge* edges = bigram_edges();
  row.reserve(end - begin);
  for (uint32_t i = begin; i < end; ++i) row.push_back(edges[i].word_id2);
  std::sort(row.begin(), row.end());  // For membership binary search.
  return row;
}

bool StaticLexicon::RowContains(const std::vector<uint32_t>& row,
                                uint32_t word_id) const {
  return std::binary_search(row.begin(), row.end(), word_id);
}

void StaticLexicon::CollectKeyEntries(uint32_t key_i, int edit_cost,
                                      int variant_cost,
                                      const std::vector<uint32_t>& bigram_row,
                                      std::vector<uint32_t>& seen_ids,
                                      std::vector<Candidate>& out) const {
  const KeyRecord& k = keys()[key_i];
  for (uint32_t e = k.entry_first; e < k.entry_first + k.entry_count; ++e) {
    const uint32_t word_id = e + 1;
    if (std::find(seen_ids.begin(), seen_ids.end(), word_id) != seen_ids.end())
      continue;
    seen_ids.push_back(word_id);

    const EntryRecord& rec = entries()[e];
    Candidate candidate;
    candidate.text = EntryText(e);
    candidate.comment = L"lexicon";
    const bool bigram_hit = RowContains(bigram_row, word_id);
    ScoreSignals signals;
    signals.system_frequency = rec.frequency;
    signals.edit_cost = edit_cost;
    signals.variant_cost = variant_cost;
    signals.bigram_hit = bigram_hit;
    candidate.score = scorer_.ScoreToInt(signals, 0);
    out.push_back(std::move(candidate));
  }
}

std::vector<Candidate> StaticLexicon::Query(const std::string& latin_input,
                                            size_t max_results) {
  std::vector<Candidate> out;
  if (!is_loaded() || latin_input.empty() || max_results == 0) return out;

  const std::vector<uint32_t> bigram_row =
      BigramRow(context_word_id_.load(std::memory_order_relaxed));
  std::vector<uint32_t> seen_ids;

  // Exact + prefix: contiguous run of keys starting with the input.
  for (uint32_t idx = LowerBoundKey(latin_input); idx < header_->key_count;
       ++idx) {
    const KeyRecord& k = keys()[idx];
    if (k.key_length < latin_input.size() ||
        std::memcmp(key_blob() + k.key_offset, latin_input.data(),
                    latin_input.size()) != 0) {
      break;  // Sorted: the prefix run has ended.
    }
    CollectKeyEntries(idx, /*edit_cost=*/0, /*variant_cost=*/0, bigram_row,
                      seen_ids, out);
  }

  // Fault tolerance only when the exact/prefix pass is thin and the input is
  // long enough that edits are meaningful (avoids matching everything). Walks
  // the DAWG and a Damerau-Levenshtein<=1 automaton in lock-step.
  if (out.size() < max_results && latin_input.size() >= 2 && dawg_.valid()) {
    LevenshteinAutomaton automaton(latin_input, 1, &equivalences_);
    IntersectDfs(dawg_.root(), automaton, automaton.Start(), 0, bigram_row,
                 seen_ids, out);
  }

  std::stable_sort(out.begin(), out.end(),
                   [](const Candidate& a, const Candidate& b) {
                     return a.score > b.score;
                   });
  if (out.size() > max_results) out.resize(max_results);
  return out;
}

void StaticLexicon::IntersectDfs(uint32_t node_index,
                                 const LevenshteinAutomaton& automaton,
                                 const LevenshteinAutomaton::State& state,
                                 uint32_t rank,
                                 const std::vector<uint32_t>& bigram_row,
                                 std::vector<uint32_t>& seen_ids,
                                 std::vector<Candidate>& out) const {
  const DawgNode& node = dawg_.node(node_index);
  // A final node spells a complete key whose lexicographic rank is `rank`,
  // which is exactly its KeyRecord index. Collect it if within edit budget.
  if (node.is_final && automaton.IsAccept(state)) {
    const int cost = automaton.Distance(state);
    if (rank < header_->key_count) {
      // Reached here through the tolerant pass. A zero edit distance means the
      // key differs from the input only by soft-equivalence substitutions
      // (o/u, q/v, h/g, t/d) -> a variant, recalled but ranked below exact.
      // A positive distance is a genuine typo.
      if (cost == 0) {
        CollectKeyEntries(rank, /*edit_cost=*/0, /*variant_cost=*/1, bigram_row,
                          seen_ids, out);
      } else {
        CollectKeyEntries(rank, /*edit_cost=*/cost, /*variant_cost=*/0,
                          bigram_row, seen_ids, out);
      }
    }
  }
  // Descend each transition in ascending label order, threading the rank:
  // words ending here and under smaller-labelled edges are all lexicographi-
  // cally smaller than anything reached through this edge.
  uint32_t child_rank = rank + (node.is_final ? 1u : 0u);
  const uint32_t first = node.trans_first;
  const uint32_t last = first + node.trans_count;
  for (uint32_t t = first; t < last; ++t) {
    const DawgTrans& edge = dawg_.trans(t);
    const LevenshteinAutomaton::State next =
        automaton.Step(state, static_cast<char>(edge.label));
    if (automaton.CanMatch(next)) {
      IntersectDfs(edge.target, automaton, next, child_rank, bigram_row,
                   seen_ids, out);
    }
    child_rank += dawg_.node(edge.target).word_count;
  }
}

uint32_t StaticLexicon::WordIdOf(const std::string& latin,
                                 const std::wstring& mongolian) const {
  if (!is_loaded()) return 0;
  const uint32_t idx = LowerBoundKey(latin);
  if (idx >= header_->key_count || CompareKey(idx, latin) != 0) return 0;
  const KeyRecord& k = keys()[idx];
  for (uint32_t e = k.entry_first; e < k.entry_first + k.entry_count; ++e) {
    if (EntryText(e) == mongolian) return e + 1;
  }
  return 0;
}

std::vector<Candidate> StaticLexicon::PredictNext(uint32_t context_word_id,
                                                  size_t max_results) const {
  std::vector<Candidate> out;
  if (!is_loaded() || header_->bigram_row_count == 0 || context_word_id == 0 ||
      context_word_id > header_->bigram_row_count || max_results == 0) {
    return out;
  }
  const uint32_t r = context_word_id - 1;
  const uint32_t begin = bigram_index()[r];
  const uint32_t end = bigram_index()[r + 1];
  const BigramEdge* edges = bigram_edges();  // Stored weight-descending.
  for (uint32_t i = begin; i < end && out.size() < max_results; ++i) {
    const uint32_t entry_index = edges[i].word_id2 - 1;
    if (entry_index >= header_->entry_count) continue;
    Candidate candidate;
    candidate.text = EntryText(entry_index);
    candidate.comment = L"predict";
    candidate.score = static_cast<int32_t>(edges[i].weight);
    out.push_back(std::move(candidate));
  }
  return out;
}

}  // namespace paiza
