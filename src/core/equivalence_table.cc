// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid

#include "core/equivalence_table.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace paiza {

void EquivalenceTable::Reset() {
  for (int i = 0; i < 256; ++i) class_of_[i] = i;  // Identity classes.
  group_count_ = 0;
}

int EquivalenceTable::LoadFromString(const std::string& content) {
  Reset();
  std::istringstream stream(content);
  std::string line;
  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    // Collect the ASCII keys on this line (whitespace-separated tokens; only
    // the first byte of each token is used).
    std::istringstream tokens(line);
    std::string token;
    int representative = -1;
    int keys_in_group = 0;
    while (tokens >> token) {
      if (token[0] == '#') break;  // Rest of the line is a comment.
      const unsigned char key = static_cast<unsigned char>(token[0]);
      ++keys_in_group;
      if (representative < 0) {
        representative = class_of_[key];
      } else {
        // Fold this key's class into the representative.
        const int old_class = class_of_[key];
        if (old_class != representative) {
          for (int i = 0; i < 256; ++i) {
            if (class_of_[i] == old_class) class_of_[i] = representative;
          }
        }
      }
    }
    if (keys_in_group >= 2) ++group_count_;  // A lone key defines nothing.
  }
  return group_count_;
}

int EquivalenceTable::LoadFromFile(const std::wstring& path) {
  std::ifstream file(std::filesystem::path(path), std::ios::binary);
  if (!file.is_open()) {
    Reset();
    return 0;
  }
  std::ostringstream buffer;
  buffer << file.rdbuf();
  return LoadFromString(buffer.str());
}

}  // namespace paiza
