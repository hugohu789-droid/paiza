// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid

#include "core/engine_c_api.h"

#include <cstdio>
#include <memory>
#include <string>

#include "common/string_utils.h"
#include "core/candidate_engine.h"
#include "core/local_dictionary_source.h"
#include "core/user_dictionary.h"

namespace {

struct EngineState {
  paiza::CandidateEngine engine;
  paiza::UserDictionary* user = nullptr;  // Owned by `engine`.
  std::string last_result;                // Backing store for Engine_Search.
};

EngineState* g_engine = nullptr;

void AppendJsonEscaped(std::string* out, const std::string& utf8) {
  for (const char c : utf8) {
    switch (c) {
      case '"':
        *out += "\\\"";
        break;
      case '\\':
        *out += "\\\\";
        break;
      case '\n':
        *out += "\\n";
        break;
      case '\r':
        *out += "\\r";
        break;
      case '\t':
        *out += "\\t";
        break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buffer[8];
          std::snprintf(buffer, sizeof(buffer), "\\u%04X",
                        static_cast<unsigned char>(c));
          *out += buffer;
        } else {
          out->push_back(c);
        }
    }
  }
}

}  // namespace

extern "C" {

bool Engine_Initialize(const char* db_path, const char* lexicon_path,
                       const char* csr_path) {
  (void)csr_path;  // Bigram/CSR: Phase 2 (docs/core-engine-spec.md §7).
  Engine_Destroy();
  g_engine = new EngineState();

  auto user = std::make_unique<paiza::UserDictionary>(
      db_path != nullptr ? paiza::Utf8ToWide(db_path) : std::wstring());
  g_engine->user = user.get();
  g_engine->engine.AddSource(std::move(user));

  bool ok = true;
  if (lexicon_path != nullptr && lexicon_path[0] != '\0') {
    auto lexicon = std::make_unique<paiza::LocalDictionarySource>();
    ok = lexicon->LoadFromFile(paiza::Utf8ToWide(lexicon_path)) > 0;
    g_engine->engine.AddSource(std::move(lexicon));
  }
  return ok;
}

const char* Engine_Search(const char* latin_input,
                          uint32_t context_word_id) {
  (void)context_word_id;  // N-gram context: Phase 2.
  static const char kEmpty[] = "{\"candidates\":[]}";
  if (g_engine == nullptr || latin_input == nullptr) return kEmpty;

  const auto candidates = g_engine->engine.Query(latin_input, 27);
  std::string& json = g_engine->last_result;
  json.clear();
  json += "{\"candidates\":[";
  bool first = true;
  for (const auto& candidate : candidates) {
    if (!first) json += ',';
    first = false;
    json += "{\"text\":\"";
    AppendJsonEscaped(&json, paiza::WideToUtf8(candidate.text));
    json += "\",\"source\":\"";
    json += candidate.comment == L"user" ? "user" : "lexicon";
    json += "\",\"freq\":";
    json += std::to_string(candidate.score);
    json += '}';
  }
  json += "]}";
  return json.c_str();
}

void Engine_CommitCandidate(const char* latin_input,
                            const char* mongolian_word_utf8) {
  if (g_engine == nullptr || g_engine->user == nullptr ||
      latin_input == nullptr || mongolian_word_utf8 == nullptr) {
    return;
  }
  g_engine->user->RecordCommit(latin_input,
                               paiza::Utf8ToWide(mongolian_word_utf8));
}

int Engine_ImportUserDictionary(const char* txt_path_utf8) {
  if (g_engine == nullptr || g_engine->user == nullptr ||
      txt_path_utf8 == nullptr) {
    return -1;
  }
  const paiza::ImportResult result =
      g_engine->user->ImportFromFile(paiza::Utf8ToWide(txt_path_utf8));
  return result.opened ? result.imported : -1;
}

void Engine_Destroy() {
  delete g_engine;  // UserDictionary's destructor flushes synchronously.
  g_engine = nullptr;
}

}  // extern "C"
