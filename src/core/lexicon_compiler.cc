// Copyright 2026 Paiza. All rights reserved.

#include "core/lexicon_compiler.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <unordered_map>

#include "common/logging.h"
#include "core/lexicon_binary.h"

namespace paiza {
namespace {

// Appends the raw bytes of a trivially-copyable struct to a byte buffer.
template <typename T>
void AppendPod(std::vector<uint8_t>& out, const T& value) {
  const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
  out.insert(out.end(), bytes, bytes + sizeof(T));
}

void AppendBytes(std::vector<uint8_t>& out, const std::string& s) {
  out.insert(out.end(), s.begin(), s.end());
}

// Splits a line on tabs. Trailing '\r' is stripped by the caller.
std::vector<std::string> SplitTabs(const std::string& line) {
  std::vector<std::string> fields;
  size_t start = 0;
  while (true) {
    const size_t tab = line.find('\t', start);
    if (tab == std::string::npos) {
      fields.push_back(line.substr(start));
      break;
    }
    fields.push_back(line.substr(start, tab - start));
    start = tab + 1;
  }
  return fields;
}

}  // namespace

void LexiconCompiler::AddEntry(std::string latin, std::string mongolian_utf8,
                               uint32_t frequency) {
  if (latin.empty() || mongolian_utf8.empty()) return;
  entries_.push_back({std::move(latin), std::move(mongolian_utf8), frequency});
}

int LexiconCompiler::AddDictionaryFile(const std::wstring& path) {
  std::ifstream file(std::filesystem::path(path), std::ios::binary);
  if (!file.is_open()) return -1;
  int accepted = 0;
  std::string line;
  while (std::getline(file, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    const std::vector<std::string> fields = SplitTabs(line);
    if (fields.size() < 2 || fields[0].empty() || fields[1].empty()) continue;
    const uint32_t freq =
        fields.size() >= 3 ? static_cast<uint32_t>(std::atoi(fields[2].c_str()))
                           : 0u;
    entries_.push_back({fields[0], fields[1], freq});
    ++accepted;
  }
  return accepted;
}

int LexiconCompiler::AddBigramFile(const std::wstring& path) {
  std::ifstream file(std::filesystem::path(path), std::ios::binary);
  if (!file.is_open()) return -1;
  int accepted = 0;
  std::string line;
  while (std::getline(file, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    const std::vector<std::string> fields = SplitTabs(line);
    if (fields.size() < 5) continue;
    const uint32_t weight = static_cast<uint32_t>(std::atoi(fields[4].c_str()));
    if (weight == 0) continue;
    bigrams_.push_back(
        {fields[0], fields[1], fields[2], fields[3], weight});
    ++accepted;
  }
  return accepted;
}

bool LexiconCompiler::Write(const std::wstring& path) const {
  if (entries_.empty()) return false;

  // 1. Stable ordering: by Latin key, then frequency desc, then word bytes.
  //    The final index of an entry becomes its word_id - 1.
  std::vector<const Entry*> ordered;
  ordered.reserve(entries_.size());
  for (const Entry& e : entries_) ordered.push_back(&e);
  std::sort(ordered.begin(), ordered.end(),
            [](const Entry* a, const Entry* b) {
              if (a->latin != b->latin) return a->latin < b->latin;
              if (a->frequency != b->frequency)
                return a->frequency > b->frequency;
              return a->mongolian < b->mongolian;
            });

  // 2. Build the string pool + entry records, and a (latin,word)->word_id map
  //    for bigram resolution.
  std::vector<uint8_t> string_pool;
  std::vector<EntryRecord> entry_records;
  entry_records.reserve(ordered.size());
  std::unordered_map<std::string, uint32_t> word_id_of;
  word_id_of.reserve(ordered.size() * 2);

  // 3. Group consecutive equal keys into KeyRecords over the unique key blob.
  std::vector<uint8_t> key_blob;
  std::vector<KeyRecord> key_records;

  for (size_t i = 0; i < ordered.size(); ++i) {
    const Entry* e = ordered[i];
    if (key_records.empty() ||
        ordered[key_records.back().entry_first]->latin != e->latin) {
      KeyRecord kr;
      kr.key_offset = static_cast<uint32_t>(key_blob.size());
      kr.key_length = static_cast<uint32_t>(e->latin.size());
      kr.entry_first = static_cast<uint32_t>(i);
      kr.entry_count = 0;
      AppendBytes(key_blob, e->latin);
      key_records.push_back(kr);
    }
    key_records.back().entry_count += 1;

    EntryRecord er;
    er.text_offset = static_cast<uint32_t>(string_pool.size());
    er.text_length = static_cast<uint32_t>(e->mongolian.size());
    er.frequency = e->frequency;
    er.key_index = static_cast<uint32_t>(key_records.size() - 1);
    AppendBytes(string_pool, e->mongolian);
    entry_records.push_back(er);

    // word_id = index + 1. First writer for a (latin,word) pair wins.
    word_id_of.emplace(e->latin + '\t' + e->mongolian,
                       static_cast<uint32_t>(i + 1));
  }

  // 4. Bigram CSR. Row r (0-based) holds edges leaving word_id r+1.
  const uint32_t entry_count = static_cast<uint32_t>(entry_records.size());
  std::vector<std::vector<BigramEdge>> rows(entry_count);
  size_t total_edges = 0;
  for (const Bigram& b : bigrams_) {
    auto it1 = word_id_of.find(b.latin1 + '\t' + b.mongolian1);
    auto it2 = word_id_of.find(b.latin2 + '\t' + b.mongolian2);
    if (it1 == word_id_of.end() || it2 == word_id_of.end()) continue;
    rows[it1->second - 1].push_back({it2->second, b.weight});
    ++total_edges;
  }
  const bool has_bigrams = total_edges > 0;
  std::vector<uint32_t> bigram_index;  // entry_count + 1 offsets.
  std::vector<BigramEdge> bigram_edges;
  if (has_bigrams) {
    bigram_index.reserve(entry_count + 1);
    bigram_edges.reserve(total_edges);
    uint32_t running = 0;
    for (uint32_t r = 0; r < entry_count; ++r) {
      bigram_index.push_back(running);
      std::sort(rows[r].begin(), rows[r].end(),
                [](const BigramEdge& a, const BigramEdge& b) {
                  return a.weight > b.weight;
                });
      for (const BigramEdge& edge : rows[r]) bigram_edges.push_back(edge);
      running += static_cast<uint32_t>(rows[r].size());
    }
    bigram_index.push_back(running);  // Sentinel = total edge count.
  }

  // 5. Assemble the payload (everything after the header), then compute CRC.
  std::vector<uint8_t> payload;
  const uint64_t header_size = sizeof(LexiconHeader);

  LexiconHeader header;
  header.key_count = static_cast<uint32_t>(key_records.size());
  header.entry_count = entry_count;
  header.bigram_row_count = has_bigrams ? entry_count : 0;
  header.bigram_edge_count = static_cast<uint32_t>(bigram_edges.size());

  uint64_t offset = header_size;
  header.keys_offset = offset;
  for (const KeyRecord& kr : key_records) AppendPod(payload, kr);
  offset += static_cast<uint64_t>(key_records.size()) * sizeof(KeyRecord);

  header.key_blob_offset = offset;
  header.key_blob_size = key_blob.size();
  payload.insert(payload.end(), key_blob.begin(), key_blob.end());
  offset += key_blob.size();

  header.entries_offset = offset;
  for (const EntryRecord& er : entry_records) AppendPod(payload, er);
  offset += static_cast<uint64_t>(entry_records.size()) * sizeof(EntryRecord);

  header.string_pool_offset = offset;
  header.string_pool_size = string_pool.size();
  payload.insert(payload.end(), string_pool.begin(), string_pool.end());
  offset += string_pool.size();

  if (has_bigrams) {
    header.bigram_index_offset = offset;
    for (uint32_t v : bigram_index) AppendPod(payload, v);
    offset += static_cast<uint64_t>(bigram_index.size()) * sizeof(uint32_t);
    header.bigram_edges_offset = offset;
    for (const BigramEdge& edge : bigram_edges) AppendPod(payload, edge);
    offset += static_cast<uint64_t>(bigram_edges.size()) * sizeof(BigramEdge);
  }

  header.file_size = header_size + payload.size();
  header.payload_crc32 = Crc32(payload.data(), payload.size());

  // 6. Write header + payload to a temp file, then rename into place so a
  //    reader never sees a half-written lexicon.
  const std::filesystem::path final_path(path);
  std::filesystem::path tmp_path = final_path;
  tmp_path += L".tmp";
  {
    std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return false;
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    out.write(reinterpret_cast<const char*>(payload.data()),
              static_cast<std::streamsize>(payload.size()));
    if (!out.good()) return false;
  }
  std::error_code ec;
  std::filesystem::rename(tmp_path, final_path, ec);
  if (ec) {
    // Cross-device or existing-target quirk: fall back to remove + rename.
    std::filesystem::remove(final_path, ec);
    std::filesystem::rename(tmp_path, final_path, ec);
    if (ec) {
      std::filesystem::remove(tmp_path, ec);
      return false;
    }
  }
  return true;
}

}  // namespace paiza
