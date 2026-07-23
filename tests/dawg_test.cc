// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid

#include "core/dawg.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "core/lexicon_binary.h"

namespace paiza {
namespace {

struct BuiltDawg {
  std::vector<DawgNode> nodes;
  std::vector<DawgTrans> trans;
  uint32_t root = 0;
};

BuiltDawg Build(std::vector<std::string> keys) {
  std::sort(keys.begin(), keys.end());
  keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
  BuiltDawg d;
  EXPECT_TRUE(BuildDawg(keys, d.nodes, d.trans, d.root));
  return d;
}

// Enumerates accepted words in lexicographic order by DFS over ascending
// edges.
void Enumerate(const BuiltDawg& d, uint32_t node, std::string& prefix,
               std::vector<std::string>& out) {
  if (d.nodes[node].is_final) out.push_back(prefix);
  const DawgNode& n = d.nodes[node];
  for (uint32_t t = n.trans_first; t < n.trans_first + n.trans_count; ++t) {
    prefix.push_back(static_cast<char>(d.trans[t].label));
    Enumerate(d, d.trans[t].target, prefix, out);
    prefix.pop_back();
  }
}

// Computes a word's rank via the word_count numbering (the same arithmetic
// StaticLexicon::IntersectDfs uses to recover the KeyRecord index).
uint32_t RankOf(const BuiltDawg& d, const std::string& word) {
  uint32_t rank = 0;
  uint32_t node = d.root;
  for (char c : word) {
    if (d.nodes[node].is_final) ++rank;
    const DawgNode& n = d.nodes[node];
    uint32_t next = node;
    for (uint32_t t = n.trans_first; t < n.trans_first + n.trans_count; ++t) {
      const char label = static_cast<char>(d.trans[t].label);
      if (label < c) {
        rank += d.nodes[d.trans[t].target].word_count;
      } else if (label == c) {
        next = d.trans[t].target;
      }
    }
    node = next;
  }
  return rank;
}

TEST(DawgTest, AcceptsExactlyTheInputKeysInOrder) {
  std::vector<std::string> keys = {"bagsi", "sain", "sar", "saraa", "mng"};
  const BuiltDawg d = Build(keys);
  std::vector<std::string> enumerated;
  std::string prefix;
  Enumerate(d, d.root, prefix, enumerated);

  std::sort(keys.begin(), keys.end());
  EXPECT_EQ(enumerated, keys);
}

TEST(DawgTest, RankMatchesSortedIndex) {
  std::vector<std::string> keys = {"a", "ab", "abc", "b", "bc", "xyz"};
  const BuiltDawg d = Build(keys);
  std::sort(keys.begin(), keys.end());
  for (uint32_t i = 0; i < keys.size(); ++i) {
    EXPECT_EQ(RankOf(d, keys[i]), i) << "key=" << keys[i];
  }
}

TEST(DawgTest, SharesSuffixesToStayMinimal) {
  // "xy" and "zy": the 'x' and 'z' states are both "not final, edge y -> leaf"
  // and so are themselves identical and merged, on top of the shared leaf.
  // A plain trie keeps 5 nodes; the minimal DAWG keeps 3 (root, the shared
  // "->y->leaf" state, and the final leaf).
  const BuiltDawg d = Build({"xy", "zy"});
  EXPECT_EQ(d.nodes.size(), 3u);
}

TEST(DawgTest, EmptyInputReturnsFalse) {
  std::vector<DawgNode> nodes;
  std::vector<DawgTrans> trans;
  uint32_t root = 0;
  EXPECT_FALSE(BuildDawg({}, nodes, trans, root));
}

}  // namespace
}  // namespace paiza
