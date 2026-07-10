// Copyright 2026 Paiza. All rights reserved.
//
// Dynamic user lexicon: the read-write half of the engine's dual-track
// (overlay) architecture — see docs/core-engine-spec.md §3.2.
//
//   * In-memory ordered map (prefix lookups via lower_bound), zero I/O on
//     the typing path.
//   * Every committed candidate bumps its frequency (RecordCommit); new
//     latin/word pairs become user-coined entries automatically.
//   * A dedicated writer thread persists dirty entries to SQLite
//     (UserHistory table, WAL) when writes are >=3s apart; destruction
//     flushes synchronously. The SQLite handle is only ever touched from
//     the writer thread (plus the initial load, before it starts).
//   * ImportFromFile: user-supplied TXT, same TAB-separated format as the
//     main dictionary; entries may be single letters or whole phrases
//     (the Mongolian field is TAB-delimited, so spaces/NNBSP inside it
//     are preserved).
//
// Thread-safe: Query / RecordCommit / ImportFromFile may be called from
// different threads (on Windows the IPC workers query while the settings
// window imports).

#ifndef PAIZA_CORE_USER_DICTIONARY_H_
#define PAIZA_CORE_USER_DICTIONARY_H_

#include <condition_variable>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "core/candidate_source.h"

struct sqlite3;

namespace paiza {

struct ImportResult {
  bool opened = false;  // File could not be opened when false.
  int imported = 0;     // Entries added or updated.
  int skipped = 0;      // Malformed lines ignored.
};

class UserDictionary : public CandidateSource {
 public:
  // `db_path`: SQLite file for persistence; created (with schema) when
  // missing. Empty path = memory-only mode (used by some tests).
  explicit UserDictionary(std::wstring db_path);
  ~UserDictionary() override;

  UserDictionary(const UserDictionary&) = delete;
  UserDictionary& operator=(const UserDictionary&) = delete;

  // CandidateSource ------------------------------------------------------
  std::wstring name() const override { return L"user"; }
  // Above the static lexicon (100): user words rank first (spec §4.2 w4).
  int priority() const override { return 200; }
  bool IsAvailable() const override;
  std::vector<Candidate> Query(const std::string& latin_input,
                               size_t max_results) override;

  // Dynamic updates --------------------------------------------------------
  // Called when a candidate is committed: bumps the frequency (or inserts
  // a new user-coined entry) and schedules async persistence.
  void RecordCommit(const std::string& latin_key, const std::wstring& word);

  // Imports `latin<TAB>mongolian[<TAB>freq]` lines (UTF-8, '#' comments).
  // Existing entries keep max(current, imported) frequency. Takes effect
  // immediately; persistence is scheduled asynchronously.
  ImportResult ImportFromFile(const std::wstring& path);

  // Blocks until every pending change reached SQLite (tests; shutdown).
  void Flush();

  size_t entry_count() const;

 private:
  struct UserWord {
    std::wstring word;
    uint32_t frequency = 1;
    int64_t last_used = 0;  // Unix seconds.
  };

  void LoadFromDatabase();
  void WriterLoop();
  // Queues the entry's current state for the writer thread. Caller holds
  // `mutex_`.
  void QueueWriteLocked(const std::string& latin_key, const UserWord& word);
  // Inserts or updates in memory. Caller holds `mutex_`. Returns the
  // stored entry.
  UserWord& UpsertLocked(const std::string& latin_key,
                         const std::wstring& word, uint32_t frequency,
                         int64_t last_used, bool add_frequencies);

  const std::wstring db_path_;
  sqlite3* db_ = nullptr;  // Writer thread only (after the initial load).

  mutable std::mutex mutex_;
  std::map<std::string, std::vector<UserWord>> entries_;
  // Pending rows keyed by (latin, word); values are state snapshots, so
  // re-queuing the same pair before a flush just overwrites the snapshot.
  std::map<std::pair<std::string, std::wstring>, UserWord> pending_;
  std::condition_variable wake_writer_;
  std::condition_variable flush_done_;
  bool stop_ = false;
  bool writer_busy_ = false;
  bool flush_requested_ = false;  // Skips the batching delay.

  std::thread writer_;
};

}  // namespace paiza

#endif  // PAIZA_CORE_USER_DICTIONARY_H_
