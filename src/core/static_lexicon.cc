// Copyright 2026 Paiza. All rights reserved.

#include "core/static_lexicon.h"

#include <algorithm>
#include <cstring>

#include "common/logging.h"
#include "common/string_utils.h"
#include "core/equivalence_table.h"
#include "core/scorer.h"

namespace paiza {
namespace {

constexpr int kMaxEditCost = 1;

}  // namespace

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
    CollectKeyEntries(idx, 0, bigram_row, seen_ids, out);
  }

  // Fault tolerance only when the exact/prefix pass is thin and the input is
  // long enough that edits are meaningful (avoids matching everything).
  if (out.size() < max_results && latin_input.size() >= 2) {
    TolerantSearch(latin_input, bigram_row, seen_ids, out);
  }

  std::stable_sort(out.begin(), out.end(),
                   [](const Candidate& a, const Candidate& b) {
                     return a.score > b.score;
                   });
  if (out.size() > max_results) out.resize(max_results);
  return out;
}

void StaticLexicon::TolerantSearch(const std::string& input,
                                   const std::vector<uint32_t>& bigram_row,
                                   std::vector<uint32_t>& seen_ids,
                                   std::vector<Candidate>& out) const {
  // Whole-word Levenshtein<=1 via an implicit-trie DFS over the sorted keys.
  const size_t n = input.size();
  std::vector<int> initial(n + 1);
  for (size_t j = 0; j <= n; ++j) initial[j] = static_cast<int>(j);
  DfsLevenshtein(input, 0, header_->key_count, 0, initial, bigram_row,
                 seen_ids, out);

  // Adjacent transposition (Damerau): swap each neighbouring pair and do an
  // exact/prefix lookup. Covers "typed two keys out of order".
  for (size_t i = 0; i + 1 < n; ++i) {
    std::string swapped = input;
    std::swap(swapped[i], swapped[i + 1]);
    if (swapped == input) continue;
    for (uint32_t idx = LowerBoundKey(swapped); idx < header_->key_count;
         ++idx) {
      const KeyRecord& k = keys()[idx];
      if (k.key_length < swapped.size() ||
          std::memcmp(key_blob() + k.key_offset, swapped.data(),
                      swapped.size()) != 0) {
        break;
      }
      CollectKeyEntries(idx, 1, bigram_row, seen_ids, out);
    }
  }
}

void StaticLexicon::DfsLevenshtein(const std::string& input, uint32_t lo,
                                   uint32_t hi, size_t depth,
                                   const std::vector<int>& prev_row,
                                   const std::vector<uint32_t>& bigram_row,
                                   std::vector<uint32_t>& seen_ids,
                                   std::vector<Candidate>& out) const {
  const size_t n = input.size();
  const KeyRecord* key_arr = keys();
  const char* blob = key_blob();

  // Keys whose length equals `depth` terminate here: a complete key matched
  // against the whole input with cost prev_row[n].
  uint32_t k = lo;
  while (k < hi && key_arr[k].key_length == depth) {
    const int cost = prev_row[n];
    // Collect cost 0..kMaxEditCost. A cost-0 hit is usually already found by
    // the exact/prefix pass (deduplicated in CollectKeyEntries), but with an
    // equivalence class an input that never matched literally can still reach
    // a key at zero cost — that is a genuine new find.
    if (cost <= kMaxEditCost) {
      CollectKeyEntries(k, cost, bigram_row, seen_ids, out);
    }
    ++k;
  }

  // Group the remaining (longer) keys by their byte at column `depth`.
  uint32_t g = k;
  while (g < hi) {
    const char c = blob[key_arr[g].key_offset + depth];
    uint32_t g2 = g;
    while (g2 < hi && blob[key_arr[g2].key_offset + depth] == c) ++g2;

    std::vector<int> row(n + 1);
    row[0] = static_cast<int>(depth) + 1;
    int row_min = row[0];
    for (size_t j = 1; j <= n; ++j) {
      const bool same = input[j - 1] == c ||
                        equivalences_.Equivalent(input[j - 1], c);
      const int sub = prev_row[j - 1] + (same ? 0 : 1);
      const int del = prev_row[j] + 1;
      const int ins = row[j - 1] + 1;
      row[j] = std::min({sub, del, ins});
      row_min = std::min(row_min, row[j]);
    }
    if (row_min <= kMaxEditCost) {
      DfsLevenshtein(input, g, g2, depth + 1, row, bigram_row, seen_ids, out);
    }
    g = g2;
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
