// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid
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

#include <atomic>
#include <condition_variable>
#include <functional>
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

// One word the user has committed, and the habit behind it. Keyed by the
// word rather than by what was typed to reach it: an external engine ranks
// words, and the same word reached through two spellings is one habit.
struct UserWordUse {
  std::wstring word;
  uint32_t count = 0;
  int64_t last_used = 0;  // Unix seconds.
};

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

  // Profile gating: a disabled dictionary reports itself unavailable, so
  // CandidateEngine skips it and another profile's entries can never reach
  // the candidate list. Atomic because settings toggle it while the IPC
  // workers query. See docs/input-profiles.md.
  void set_enabled(bool enabled) { enabled_.store(enabled); }
  bool enabled() const { return enabled_.load(); }

  // Whether this dictionary answers queries itself, as opposed to only
  // storing. Turned off for a profile whose engine takes the user's words
  // (`engine_user_words`): there the engine ranks them, and a source
  // sitting above the engine would put every committed word first for ever,
  // which is the opposite of a habit that decays. Storing, persistence and
  // the commit listener all carry on regardless. Separate from
  // `set_enabled`, which is profile gating and means something else.
  void set_answers_queries(bool answers) { answers_queries_.store(answers); }
  bool answers_queries() const { return answers_queries_.load(); }

  size_t entry_count() const;

  // Every committed word with its habit, strongest first. Several latin
  // keys can reach the same word, so these are merged: the counts add up and
  // the latest use wins. For a host handing the user's habits to an external
  // engine, which ranks words and knows nothing about what was typed to
  // reach them.
  std::vector<UserWordUse> WordsByUse() const;

  // Called after every RecordCommit, outside this class's lock, with the
  // word and when it was committed. One listener; setting a new one replaces
  // it, and an empty function detaches. Exists so that an external engine
  // learns the same commits this dictionary does without every front-end
  // having to remember to tell it -- there are three of them, and the one
  // that forgot would be a profile whose habits silently did not stick.
  using CommitListener =
      std::function<void(const std::wstring& word, int64_t unix_seconds)>;
  void set_commit_listener(CommitListener listener);

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
  std::atomic<bool> enabled_{true};
  std::atomic<bool> answers_queries_{true};

  mutable std::mutex listener_mutex_;
  CommitListener commit_listener_;
};

}  // namespace paiza

#endif  // PAIZA_CORE_USER_DICTIONARY_H_
