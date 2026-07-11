// Copyright 2026 Paiza. All rights reserved.
//
// Minimal, numbered directed acyclic word graph (DAWG) over the lexicon's
// sorted unique keys. Unlike a trie, a DAWG shares both prefixes and suffixes,
// so a large key set collapses to far fewer states. "Numbered" means every
// state carries the count of keys accepted in its sub-language, which yields a
// minimal perfect hash: the lexicographic rank of an accepted key equals its
// index in the sorted KeyRecord table. That lets tolerant search recover the
// matching entry without storing a key->id map.
//
// DawgBuilder runs offline (Daciuk incremental minimisation, sorted input).
// DawgView is a zero-copy reader over the mmap'd on-disk arrays.

#ifndef PAIZA_CORE_DAWG_H_
#define PAIZA_CORE_DAWG_H_

#include <cstdint>
#include <string>
#include <vector>

#include "core/lexicon_binary.h"

namespace paiza {

// Builds a minimal numbered DAWG from sorted, unique, non-empty keys.
// Fills dense node/transition arrays (root index in out_root) ready to
// serialise. Returns false only if `keys` is empty.
bool BuildDawg(const std::vector<std::string>& keys,
               std::vector<DawgNode>& out_nodes,
               std::vector<DawgTrans>& out_trans, uint32_t& out_root);

// Read-only view over the on-disk DAWG arrays (all indices validated by the
// caller against the header counts before use).
class DawgView {
 public:
  DawgView() = default;
  DawgView(const DawgNode* nodes, uint32_t node_count, const DawgTrans* trans,
           uint32_t trans_count, uint32_t root)
      : nodes_(nodes),
        node_count_(node_count),
        trans_(trans),
        trans_count_(trans_count),
        root_(root) {}

  bool valid() const { return nodes_ != nullptr && node_count_ > 0; }
  uint32_t root() const { return root_; }
  uint32_t node_count() const { return node_count_; }
  uint32_t trans_count() const { return trans_count_; }
  const DawgNode& node(uint32_t i) const { return nodes_[i]; }
  const DawgTrans& trans(uint32_t i) const { return trans_[i]; }

 private:
  const DawgNode* nodes_ = nullptr;
  uint32_t node_count_ = 0;
  const DawgTrans* trans_ = nullptr;
  uint32_t trans_count_ = 0;
  uint32_t root_ = 0;
};

}  // namespace paiza

#endif  // PAIZA_CORE_DAWG_H_
