// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid
//
// C-style FFI for external cross-platform hosts (C# Avalonia, Qt, ...).
// See docs/core-engine-spec.md §5 for the full contract. The in-repo
// front-ends (TSF/IMK/IBus) use paiza::InputSession directly; this
// wrapper exists for hosts that cannot link C++.
//
// Contract highlights:
//   * NOT thread-safe: call every Engine_* function from one thread.
//     (The user-lexicon writer thread is internal and invisible here.)
//   * Engine_Search returns UTF-8 JSON owned by the engine; the pointer
//     is invalidated by the next Engine_* call. Copy if you keep it.
//   * All path/string parameters are UTF-8.

#ifndef PAIZA_CORE_ENGINE_C_API_H_
#define PAIZA_CORE_ENGINE_C_API_H_

#include <stdint.h>

#if defined(_WIN32)
#define PAIZA_API __declspec(dllexport)
#else
#define PAIZA_API __attribute__((visibility("default")))
#endif

extern "C" {

// Initializes the engine. `db_path`: SQLite user-lexicon file (schema is
// created when missing; empty/NULL = memory-only). `lexicon_path`: static
// dictionary (Phase 1: TAB-separated TXT; Phase 2: FST binary).
// `csr_path`: bigram matrix, reserved for Phase 2 (pass NULL). Returns
// false when a lexicon path was given but could not be loaded.
PAIZA_API bool Engine_Initialize(const char* db_path,
                                 const char* lexicon_path,
                                 const char* csr_path);

// Candidate search for a Latin key sequence. `context_word_id` feeds the
// N-gram prediction (Phase 2); pass 0 when there is no context. Returns
// JSON: {"candidates":[{"text":"...","source":"user|lexicon","freq":N}]}
PAIZA_API const char* Engine_Search(const char* latin_input,
                                    uint32_t context_word_id);

// Commit notification: updates the dynamic user lexicon (in-memory
// immediately, persisted asynchronously).
PAIZA_API void Engine_CommitCandidate(const char* latin_input,
                                      const char* mongolian_word_utf8);

// Imports a user-dictionary TXT (latin<TAB>mongolian[<TAB>freq]; entries
// may be single letters or phrases). Returns the number of imported
// entries, or -1 when the file could not be opened.
PAIZA_API int Engine_ImportUserDictionary(const char* txt_path_utf8);

// Flushes the user lexicon and releases everything.
PAIZA_API void Engine_Destroy();

}  // extern "C"

#endif  // PAIZA_CORE_ENGINE_C_API_H_
