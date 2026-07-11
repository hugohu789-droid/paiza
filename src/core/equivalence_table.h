// Copyright 2026 Paiza. All rights reserved.
//
// Data-driven key-equivalence classes for fault-tolerant matching. Keys that
// share a class are interchangeable at zero edit cost, so common "same sound,
// different key" confusions do not count against a candidate. Loaded from
// equivalences.txt (each non-comment line lists whitespace-separated keys that
// are mutually equivalent). Empty table => every key is its own class and the
// lookups are effectively free.

#ifndef PAIZA_CORE_EQUIVALENCE_TABLE_H_
#define PAIZA_CORE_EQUIVALENCE_TABLE_H_

#include <array>
#include <string>

namespace paiza {

class EquivalenceTable {
 public:
  EquivalenceTable() { Reset(); }

  // Replaces the table from a file. Missing file leaves the table empty
  // (identity classes). Returns the number of groups parsed.
  int LoadFromFile(const std::wstring& path);

  // Parses in-memory content (same format); used by tests.
  int LoadFromString(const std::string& content);

  bool empty() const { return group_count_ == 0; }

  // Two keys are equivalent if they were listed in the same group, or are the
  // same character. Only ASCII keys participate; others compare by identity.
  bool Equivalent(char a, char b) const {
    return ClassOf(a) == ClassOf(b);
  }

 private:
  void Reset();
  int ClassOf(unsigned char c) const { return class_of_[c]; }

  // class_of_[c] is the representative class id for byte c (identity by
  // default). Non-ASCII bytes keep their own value.
  std::array<int, 256> class_of_;
  int group_count_ = 0;
};

}  // namespace paiza

#endif  // PAIZA_CORE_EQUIVALENCE_TABLE_H_
