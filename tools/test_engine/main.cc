// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid
//
// A minimal conforming external engine, built only for the test suite.
//
// It exists because the interesting half of a transport is what happens when
// the engine misbehaves, and that cannot be checked by inspection: the
// overlapped read on Windows, the poll() deadline on POSIX, EOF, and an
// oversized line are all real I/O. Flags make each of those reproducible:
//
//   (none)      a conforming toy engine
//   --hang      reads a request and never answers
//   --flood     answers with a line longer than the host's cap
//   --die       exits after the first request, without answering
//   --slow-hello  delays only the handshake
//   --garbage   answers with something that is not OK or ERR
//
// Deliberately freestanding: plain stdio, no Paiza headers, so it is also a
// worked example of how little an engine has to do (contract §5).

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace {

void Sleep(int seconds) {
#if defined(_WIN32)
  _sleep(static_cast<unsigned long>(seconds) * 1000);
#else
  sleep(static_cast<unsigned>(seconds));
#endif
}

std::vector<std::string> Split(const std::string& line, char sep) {
  std::vector<std::string> out;
  size_t begin = 0;
  while (true) {
    const size_t at = line.find(sep, begin);
    if (at == std::string::npos) {
      out.push_back(line.substr(begin));
      return out;
    }
    out.push_back(line.substr(begin, at - begin));
    begin = at + 1;
  }
}

// One line out, flushed: a response the host never sees is the same as no
// response, and stdout to a pipe is block-buffered by default.
void Say(const std::string& line) {
  std::cout << line << "\n";
  std::cout.flush();
}

// The toy "shaping" rule: the keys typed, plus an upper-cased variant, so a
// test can tell candidates apart without depending on a real engine.
std::string Upper(const std::string& text) {
  std::string out = text;
  for (char& c : out) {
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
  }
  return out;
}

}  // namespace

int main(int argc, char** argv) {
  bool hang = false, flood = false, die = false, garbage = false;
  bool slow_hello = false;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--hang") hang = true;
    else if (arg == "--flood") flood = true;
    else if (arg == "--die") die = true;
    else if (arg == "--garbage") garbage = true;
    else if (arg == "--slow-hello") slow_hello = true;
  }

  std::string keys;
  std::string line;
  while (std::getline(std::cin, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const std::vector<std::string> fields = Split(line, ' ');
    const std::string& verb = fields[0];

    if (verb == "HELLO") {
      if (slow_hello) Sleep(30);
      Say("OK\tpaiza-engine\t1");
      continue;
    }
    if (verb == "QUIT") {
      Say("OK");
      return 0;
    }

    // Misbehaviour applies to everything after the handshake, so a test can
    // get a working source and then break it.
    if (die) return 3;
    if (hang) {
      Sleep(30);  // far past the host's timeout; the host gives up first
      return 0;
    }
    if (garbage) {
      Say("WAT\tthis is not a valid response");
      continue;
    }
    if (flood) {
      Say("OK\t" + std::string(200 * 1024, 'x'));
      continue;
    }

    if (verb == "TYPE" && fields.size() >= 3) {
      keys += fields[2];
    } else if (verb == "UNTYPE") {
      if (!keys.empty()) keys.pop_back();
    } else if (verb == "CLEAR") {
      keys.clear();
      Say("OK");
      continue;
    } else if (verb == "CAND") {
      // falls through to the reply below
    } else {
      Say("ERR\tunknown request");
      continue;
    }

    if (keys.empty()) {
      Say("OK");
      continue;
    }
    Say("OK\t" + keys + "\t" + Upper(keys));
  }
  // stdin closed without QUIT: the host is gone, so exit quietly.
  return 0;
}
