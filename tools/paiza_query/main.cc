// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid
//
// Headless query REPL: type a Latin key sequence, get the candidate list back
// — no IME host, no UI, pure engine. Wires the same pipeline the real
// front-ends use (key mapping -> CandidateEngine over the static lexicon +
// user dictionary, through InputSession), so it exercises exact/prefix,
// fault-tolerant, and bigram-context behaviour.
//
// Usage:
//   paiza_query [--lexicon lexicon.bin] [--dict dictionary.txt]
//               [--keymap keymap.txt] [--user user.db] [seq ...]
//
// With trailing sequences it prints their results and exits (scriptable).
// With none it reads sequences from stdin, one per line. REPL commands:
//   :N     commit candidate N of the last result (records it to the user
//          dictionary and sets it as the bigram context for the next query)
//   :reset clear the bigram context
//   :q     quit

#include <cstdint>
#include <cstdio>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#include "common/string_utils.h"
#include "core/candidate_engine.h"
#include "core/input_session.h"
#include "core/key_mapping_store.h"
#include "core/local_dictionary_source.h"
#include "core/static_lexicon.h"
#include "core/user_dictionary.h"

namespace {

std::wstring Widen(const char* s) { return paiza::Utf8ToWide(s); }

// "U+1830 U+1820 ..." for a wide string (BMP codepoints; the Mongolian block
// needs no surrogate handling).
std::string Codepoints(const std::wstring& w) {
  std::string out;
  char buf[16];
  for (wchar_t c : w) {
    std::snprintf(buf, sizeof(buf), "U+%04X ", static_cast<unsigned>(c));
    out += buf;
  }
  if (!out.empty()) out.pop_back();
  return out;
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
  // Emit UTF-8 so the Mongolian output isn't mis-decoded in the console's
  // default (non-UTF-8) code page. The U+ codepoints stay readable either
  // way; a console font without Mongolian glyphs shows boxes, not garbage.
  ::SetConsoleOutputCP(CP_UTF8);
#endif
  std::wstring lexicon_path = L"lexicon.bin";
  std::wstring dict_path = L"dictionary.txt";
  std::wstring keymap_path;   // Empty => built-in default mapping.
  std::wstring user_path;     // Empty => in-memory user dictionary.
  std::vector<std::string> sequences;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--lexicon" && i + 1 < argc) lexicon_path = Widen(argv[++i]);
    else if (arg == "--dict" && i + 1 < argc) dict_path = Widen(argv[++i]);
    else if (arg == "--keymap" && i + 1 < argc) keymap_path = Widen(argv[++i]);
    else if (arg == "--user" && i + 1 < argc) user_path = Widen(argv[++i]);
    else if (arg.rfind("--", 0) == 0) {
      std::fprintf(stderr, "Unknown flag: %s\n", arg.c_str());
      return 2;
    } else {
      sequences.push_back(arg);
    }
  }

  // Build the engine: user overlay (priority 200) > static lexicon (100) >
  // plain-text dictionary fallback.
  paiza::CandidateEngine engine;
  auto user = std::make_unique<paiza::UserDictionary>(user_path);
  paiza::UserDictionary* user_ptr = user.get();
  engine.AddSource(std::move(user));

  auto lexicon = std::make_unique<paiza::StaticLexicon>();
  paiza::StaticLexicon* lexicon_ptr = nullptr;
  if (lexicon->Load(lexicon_path)) {
    lexicon_ptr = lexicon.get();
    engine.AddSource(std::move(lexicon));
    std::fprintf(stderr, "[loaded lexicon: %u entries, %u DAWG nodes]\n",
                 lexicon_ptr->entry_count(), lexicon_ptr->dawg_node_count());
  } else {
    std::fprintf(stderr, "[no compiled lexicon at %s; using text dict only]\n",
                 paiza::WideToUtf8(lexicon_path).c_str());
  }

  auto dict = std::make_unique<paiza::LocalDictionarySource>();
  if (dict->LoadFromFile(dict_path) > 0) engine.AddSource(std::move(dict));

  paiza::KeyMappingStore mappings(keymap_path);
  paiza::InputSession session(&engine, &mappings);
  session.set_user_dictionary(user_ptr);
  session.set_static_lexicon(lexicon_ptr);
  if (lexicon_ptr != nullptr) {
    session.set_fixed_sequences(&lexicon_ptr->fixed_sequence_table());
  }

  auto run = [&](const std::string& seq) {
    session.Reset();
    for (char c : seq) session.HandleLetter(c);
    const std::wstring& preedit = session.preedit();
    std::printf("> %s\n", seq.c_str());
    std::printf("  preedit: %s  (%s)\n", paiza::WideToUtf8(preedit).c_str(),
                Codepoints(preedit).c_str());
    const auto& cands = session.candidates();
    if (cands.empty()) {
      std::printf("  (no candidates)\n");
      return;
    }
    for (size_t i = 0; i < cands.size(); ++i) {
      std::printf("  %zu. %s  [%s]  score=%d  %s\n", i + 1,
                  paiza::WideToUtf8(cands[i].text).c_str(),
                  paiza::WideToUtf8(cands[i].comment).c_str(), cands[i].score,
                  Codepoints(cands[i].text).c_str());
    }
  };

  if (!sequences.empty()) {
    for (const std::string& seq : sequences) run(seq);
    return 0;
  }

  // Interactive stdin loop.
  std::string line;
  while (std::getline(std::cin, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) continue;
    if (line == ":q" || line == ":quit") break;
    if (line == ":reset") {
      if (lexicon_ptr != nullptr) lexicon_ptr->SetContextWord(0);
      std::printf("  [context cleared]\n");
      continue;
    }
    if (line[0] == ':') {
      const int n = std::atoi(line.c_str() + 1);
      const auto& cands = session.candidates();
      if (n >= 1 && static_cast<size_t>(n) <= cands.size()) {
        const std::wstring chosen = cands[n - 1].text;
        session.CommitCandidate(static_cast<size_t>(n - 1));
        std::printf("  [committed %s; context set for next query]\n",
                    paiza::WideToUtf8(chosen).c_str());
      } else {
        std::printf("  [no candidate %d]\n", n);
      }
      continue;
    }
    run(line);
  }
  return 0;
}
