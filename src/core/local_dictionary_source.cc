// Copyright 2026 Paiza. All rights reserved.

#include "core/local_dictionary_source.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "common/logging.h"
#include "common/string_utils.h"

namespace paiza {
namespace {

// Returns true if `text` starts with `prefix`.
bool StartsWith(const std::string& text, const std::string& prefix) {
  return text.size() >= prefix.size() &&
         text.compare(0, prefix.size(), prefix) == 0;
}

}  // namespace

size_t LocalDictionarySource::LoadFromFile(const std::wstring& path) {
  entries_.clear();
  std::ifstream file(std::filesystem::path(path), std::ios::binary);
  if (!file.is_open()) {
    PAIZA_LOG_WARN(L"Dictionary file not found: " + path);
    return 0;
  }
  std::string line;
  while (std::getline(file, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;

    // Fields: latin \t mongolian(utf8) \t frequency.
    const size_t tab1 = line.find('\t');
    if (tab1 == std::string::npos) continue;
    const size_t tab2 = line.find('\t', tab1 + 1);

    Entry entry;
    entry.latin_key = line.substr(0, tab1);
    const std::string word_utf8 =
        (tab2 == std::string::npos) ? line.substr(tab1 + 1)
                                    : line.substr(tab1 + 1, tab2 - tab1 - 1);
    entry.mongolian = Utf8ToWide(word_utf8);
    if (tab2 != std::string::npos) {
      entry.frequency = std::atoi(line.c_str() + tab2 + 1);
    }
    if (entry.latin_key.empty() || entry.mongolian.empty()) continue;
    entries_.push_back(std::move(entry));
  }
  SortEntries();
  Logf(LogLevel::kInfo, L"Dictionary loaded: %zu entries", entries_.size());
  return entries_.size();
}

void LocalDictionarySource::AddEntry(std::string latin_key,
                                     std::wstring mongolian, int frequency) {
  entries_.push_back(
      {std::move(latin_key), std::move(mongolian), frequency});
  SortEntries();
}

void LocalDictionarySource::SortEntries() {
  std::sort(entries_.begin(), entries_.end(),
            [](const Entry& a, const Entry& b) {
              return a.latin_key < b.latin_key;
            });
}

std::vector<Candidate> LocalDictionarySource::Query(
    const std::string& latin_input, size_t max_results) {
  std::vector<Candidate> results;
  // Case-sensitive: upper-case keys are distinct Mongolian letters
  // (Shift second set), so "N" and "n" must not be conflated.
  const std::string& key = latin_input;
  if (key.empty()) return results;

  // Binary search for the first entry >= key, then walk the prefix range.
  auto it = std::lower_bound(entries_.begin(), entries_.end(), key,
                             [](const Entry& e, const std::string& k) {
                               return e.latin_key < k;
                             });
  std::vector<const Entry*> matches;
  for (; it != entries_.end() && StartsWith(it->latin_key, key); ++it) {
    matches.push_back(&*it);
  }
  // Exact matches first, then by frequency.
  std::stable_sort(matches.begin(), matches.end(),
                   [&key](const Entry* a, const Entry* b) {
                     const bool exact_a = a->latin_key == key;
                     const bool exact_b = b->latin_key == key;
                     if (exact_a != exact_b) return exact_a;
                     return a->frequency > b->frequency;
                   });
  for (const Entry* entry : matches) {
    if (results.size() >= max_results) break;
    Candidate candidate;
    candidate.text = entry->mongolian;
    candidate.score = entry->frequency;
    results.push_back(std::move(candidate));
  }
  return results;
}

}  // namespace paiza
