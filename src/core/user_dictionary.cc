// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid

#include "core/user_dictionary.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>

#include "common/logging.h"
#include "common/string_utils.h"
#include "third_party/sqlite/sqlite3.h"

namespace paiza {
namespace {

// Writes are batched: the writer thread sleeps until woken and then
// drains everything queued, so bursts of typing produce one transaction.
constexpr std::chrono::seconds kWriteDelay{3};

constexpr char kSchemaSql[] =
    "CREATE TABLE IF NOT EXISTS UserHistory ("
    "  latin_key           TEXT NOT NULL,"
    "  mongolian_word      TEXT NOT NULL,"
    "  user_frequency      INTEGER DEFAULT 1,"
    "  last_used_timestamp INTEGER DEFAULT 0,"
    "  PRIMARY KEY (latin_key, mongolian_word));";

constexpr char kUpsertSql[] =
    "INSERT INTO UserHistory"
    "  (latin_key, mongolian_word, user_frequency, last_used_timestamp)"
    "  VALUES (?1, ?2, ?3, ?4)"
    "  ON CONFLICT(latin_key, mongolian_word) DO UPDATE SET"
    "    user_frequency = excluded.user_frequency,"
    "    last_used_timestamp = excluded.last_used_timestamp;";

int64_t NowSeconds() {
  return static_cast<int64_t>(std::time(nullptr));
}

bool StartsWith(const std::string& text, const std::string& prefix) {
  return text.size() >= prefix.size() &&
         text.compare(0, prefix.size(), prefix) == 0;
}

}  // namespace

UserDictionary::UserDictionary(std::wstring db_path)
    : db_path_(std::move(db_path)) {
  if (!db_path_.empty()) {
    std::error_code ec;
    std::filesystem::create_directories(
        std::filesystem::path(db_path_).parent_path(), ec);
    const std::string utf8_path = WideToUtf8(db_path_);
    if (sqlite3_open(utf8_path.c_str(), &db_) != SQLITE_OK) {
      Logf(LogLevel::kWarning, L"User dictionary: cannot open %ls",
           db_path_.c_str());
      if (db_ != nullptr) sqlite3_close(db_);
      db_ = nullptr;
    } else {
      // WAL keeps the async writer from blocking readers of the file.
      sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr,
                   nullptr);
      sqlite3_exec(db_, kSchemaSql, nullptr, nullptr, nullptr);
      LoadFromDatabase();
    }
  }
  writer_ = std::thread([this] { WriterLoop(); });
}

UserDictionary::~UserDictionary() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stop_ = true;
  }
  wake_writer_.notify_all();
  if (writer_.joinable()) writer_.join();  // Drains pending_ before exit.
  if (db_ != nullptr) sqlite3_close(db_);
}

void UserDictionary::LoadFromDatabase() {
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_,
                         "SELECT latin_key, mongolian_word, user_frequency,"
                         " last_used_timestamp FROM UserHistory;",
                         -1, &stmt, nullptr) != SQLITE_OK) {
    return;
  }
  size_t loaded = 0;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    const char* latin =
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    const char* word =
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    if (latin == nullptr || word == nullptr) continue;
    std::lock_guard<std::mutex> lock(mutex_);
    UpsertLocked(latin, Utf8ToWide(word),
                 static_cast<uint32_t>(
                     std::max<int64_t>(1, sqlite3_column_int64(stmt, 2))),
                 sqlite3_column_int64(stmt, 3),
                 /*add_frequencies=*/false);
    ++loaded;
  }
  sqlite3_finalize(stmt);
  Logf(LogLevel::kInfo, L"User dictionary: %zu entries loaded", loaded);
}

bool UserDictionary::IsAvailable() const {
  if (!enabled_.load()) return false;
  std::lock_guard<std::mutex> lock(mutex_);
  return !entries_.empty();
}

size_t UserDictionary::entry_count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  size_t count = 0;
  for (const auto& [key, words] : entries_) count += words.size();
  return count;
}

UserDictionary::UserWord& UserDictionary::UpsertLocked(
    const std::string& latin_key, const std::wstring& word,
    uint32_t frequency, int64_t last_used, bool add_frequencies) {
  std::vector<UserWord>& words = entries_[latin_key];
  for (UserWord& existing : words) {
    if (existing.word == word) {
      existing.frequency = add_frequencies
                               ? existing.frequency + frequency
                               : std::max(existing.frequency, frequency);
      existing.last_used = std::max(existing.last_used, last_used);
      return existing;
    }
  }
  words.push_back(UserWord{word, std::max<uint32_t>(1, frequency),
                           last_used});
  return words.back();
}

void UserDictionary::QueueWriteLocked(const std::string& latin_key,
                                      const UserWord& word) {
  if (db_ == nullptr) return;  // Memory-only mode.
  pending_[{latin_key, word.word}] = word;
}

void UserDictionary::RecordCommit(const std::string& latin_key,
                                  const std::wstring& word) {
  if (latin_key.empty() || word.empty()) return;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    UserWord& entry = UpsertLocked(latin_key, word, /*frequency=*/1,
                                   NowSeconds(), /*add_frequencies=*/true);
    QueueWriteLocked(latin_key, entry);
  }
  wake_writer_.notify_all();
}

ImportResult UserDictionary::ImportFromFile(const std::wstring& path) {
  ImportResult result;
  std::ifstream file(std::filesystem::path(path), std::ios::binary);
  if (!file.is_open()) return result;
  result.opened = true;

  std::string line;
  while (std::getline(file, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;

    // latin_key <TAB> mongolian [<TAB> frequency]. The Mongolian field is
    // TAB-delimited, so single letters and multi-word phrases (spaces /
    // NNBSP inside) are both valid entries.
    const size_t tab1 = line.find('\t');
    if (tab1 == std::string::npos || tab1 == 0) {
      ++result.skipped;
      continue;
    }
    const size_t tab2 = line.find('\t', tab1 + 1);
    const std::string latin = line.substr(0, tab1);
    const std::string word_utf8 =
        tab2 == std::string::npos
            ? line.substr(tab1 + 1)
            : line.substr(tab1 + 1, tab2 - tab1 - 1);
    const std::wstring word = Utf8ToWide(word_utf8);
    if (word.empty()) {
      ++result.skipped;
      continue;
    }
    uint32_t frequency = 1;
    if (tab2 != std::string::npos) {
      const long parsed = std::atol(line.c_str() + tab2 + 1);
      if (parsed > 0) frequency = static_cast<uint32_t>(parsed);
    }

    std::lock_guard<std::mutex> lock(mutex_);
    UserWord& entry = UpsertLocked(latin, word, frequency, NowSeconds(),
                                   /*add_frequencies=*/false);
    QueueWriteLocked(latin, entry);
    ++result.imported;
  }
  wake_writer_.notify_all();
  Logf(LogLevel::kInfo, L"User dictionary import: %d entries, %d skipped",
       result.imported, result.skipped);
  return result;
}

std::vector<Candidate> UserDictionary::Query(const std::string& latin_input,
                                             size_t max_results) {
  std::vector<Candidate> results;
  if (latin_input.empty() || max_results == 0) return results;

  struct Hit {
    const UserWord* word;
    bool exact;
  };
  std::vector<Hit> hits;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = entries_.lower_bound(latin_input);
         it != entries_.end() && StartsWith(it->first, latin_input); ++it) {
      const bool exact = it->first == latin_input;
      for (const UserWord& word : it->second) {
        hits.push_back({&word, exact});
      }
    }
    // Exact key first, then by user frequency, then recency.
    std::stable_sort(hits.begin(), hits.end(),
                     [](const Hit& a, const Hit& b) {
                       if (a.exact != b.exact) return a.exact;
                       if (a.word->frequency != b.word->frequency) {
                         return a.word->frequency > b.word->frequency;
                       }
                       return a.word->last_used > b.word->last_used;
                     });
    for (const Hit& hit : hits) {
      if (results.size() >= max_results) break;
      Candidate candidate;
      candidate.text = hit.word->word;
      candidate.comment = L"user";
      candidate.score = static_cast<int32_t>(std::min<uint32_t>(
          hit.word->frequency, INT32_MAX));
      results.push_back(std::move(candidate));
    }
  }
  return results;
}

void UserDictionary::Flush() {
  std::unique_lock<std::mutex> lock(mutex_);
  flush_requested_ = true;
  wake_writer_.notify_all();
  flush_done_.wait(lock, [this] {
    return pending_.empty() && !writer_busy_;
  });
  flush_requested_ = false;
}

void UserDictionary::WriterLoop() {
  for (;;) {
    std::map<std::pair<std::string, std::wstring>, UserWord> batch;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      if (pending_.empty()) {
        flush_done_.notify_all();
        wake_writer_.wait(lock,
                          [this] { return stop_ || !pending_.empty(); });
      }
      if (pending_.empty() && stop_) return;
      if (!stop_ && !flush_requested_) {
        // Batch bursts: wait out the write delay, collecting more work.
        // A Flush() or shutdown skips the delay.
        wake_writer_.wait_for(lock, kWriteDelay, [this] {
          return stop_ || flush_requested_;
        });
      }
      batch.swap(pending_);
      writer_busy_ = true;
    }

    if (db_ != nullptr && !batch.empty()) {
      sqlite3_exec(db_, "BEGIN;", nullptr, nullptr, nullptr);
      sqlite3_stmt* stmt = nullptr;
      if (sqlite3_prepare_v2(db_, kUpsertSql, -1, &stmt, nullptr) ==
          SQLITE_OK) {
        for (const auto& [key, word] : batch) {
          const std::string word_utf8 = WideToUtf8(key.second);
          sqlite3_bind_text(stmt, 1, key.first.c_str(), -1,
                            SQLITE_TRANSIENT);
          sqlite3_bind_text(stmt, 2, word_utf8.c_str(), -1,
                            SQLITE_TRANSIENT);
          sqlite3_bind_int64(stmt, 3, word.frequency);
          sqlite3_bind_int64(stmt, 4, word.last_used);
          sqlite3_step(stmt);
          sqlite3_reset(stmt);
          sqlite3_clear_bindings(stmt);
        }
        sqlite3_finalize(stmt);
      }
      sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);
    }

    {
      std::lock_guard<std::mutex> lock(mutex_);
      writer_busy_ = false;
    }
    flush_done_.notify_all();
  }
}

}  // namespace paiza
