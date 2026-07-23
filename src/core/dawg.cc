// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid

#include "core/dawg.h"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

namespace paiza {
namespace {

// Mutable state used only during construction.
struct MutNode {
  bool is_final = false;
  std::vector<std::pair<char, int>> edges;  // Kept sorted by label (ascending).
};

// Canonical signature of a node: finality plus its (label -> canonical child)
// edges. Two nodes with equal signatures accept the same sub-language and can
// be merged. Children are already canonical when a node is registered
// (bottom-up), so comparing child indices is sound.
std::string Signature(const MutNode& n) {
  std::string sig;
  sig.push_back(n.is_final ? '1' : '0');
  for (const auto& [label, child] : n.edges) {
    sig.push_back(label);
    sig.append(reinterpret_cast<const char*>(&child), sizeof(child));
  }
  return sig;
}

// Daciuk incremental minimiser for sorted input.
class Minimiser {
 public:
  Minimiser() { nodes_.emplace_back(); }  // Root = index 0.

  void Add(const std::string& word) {
    // Longest common prefix with the previous word walks the still-mutable
    // "active path" (nodes for the previous word, not yet minimised).
    size_t common = 0;
    while (common < word.size() && common < previous_.size() &&
           word[common] == previous_[common]) {
      ++common;
    }
    MinimiseFrom(common);  // Freeze the previous word's diverging suffix.

    int node = active_path_[common];
    for (size_t i = common; i < word.size(); ++i) {
      const int child = static_cast<int>(nodes_.size());
      nodes_.emplace_back();
      nodes_[node].edges.emplace_back(word[i], child);
      node = child;
    }
    nodes_[node].is_final = true;

    active_path_.resize(common + 1);
    node = active_path_[common];
    for (size_t i = common; i < word.size(); ++i) {
      node = nodes_[node].edges.back().second;
      active_path_.push_back(node);
    }
    previous_ = word;
  }

  // Finish and emit dense arrays reachable from the (canonical) root.
  void Finish(std::vector<DawgNode>& out_nodes,
              std::vector<DawgTrans>& out_trans, uint32_t& out_root) {
    MinimiseFrom(0);
    const int root = active_path_.empty() ? 0 : active_path_[0];
    Serialize(root, out_nodes, out_trans, out_root);
  }

 private:
  // Canonicalises the active path from its deepest node down to depth `down`,
  // registering each node or replacing it with an existing equivalent.
  void MinimiseFrom(size_t down) {
    for (size_t depth = active_path_.size(); depth > down + 1; --depth) {
      const int parent = active_path_[depth - 2];
      const int child = active_path_[depth - 1];
      const int canonical = RegisterOrGet(child);
      nodes_[parent].edges.back().second = canonical;
    }
    active_path_.resize(std::min(active_path_.size(), down + 1));
  }

  int RegisterOrGet(int node) {
    const std::string sig = Signature(nodes_[node]);
    auto it = register_.find(sig);
    if (it != register_.end()) return it->second;  // Merge with existing.
    register_.emplace(sig, node);
    return node;
  }

  // Word count of a node's sub-language, memoised over the resulting DAG.
  uint32_t WordCount(int node, std::unordered_map<int, uint32_t>& memo) {
    auto it = memo.find(node);
    if (it != memo.end()) return it->second;
    uint32_t count = nodes_[node].is_final ? 1u : 0u;
    for (const auto& [label, child] : nodes_[node].edges) {
      (void)label;
      count += WordCount(child, memo);
    }
    memo[node] = count;
    return count;
  }

  void Serialize(int root, std::vector<DawgNode>& out_nodes,
                 std::vector<DawgTrans>& out_trans, uint32_t& out_root) {
    std::unordered_map<int, uint32_t> word_memo;
    // Assign dense indices to reachable canonical nodes (BFS/DFS from root).
    std::unordered_map<int, uint32_t> dense;
    std::vector<int> order;
    std::vector<int> stack = {root};
    dense[root] = 0;
    order.push_back(root);
    while (!stack.empty()) {
      const int n = stack.back();
      stack.pop_back();
      for (const auto& [label, child] : nodes_[n].edges) {
        (void)label;
        if (dense.find(child) == dense.end()) {
          dense[child] = static_cast<uint32_t>(order.size());
          order.push_back(child);
          stack.push_back(child);
        }
      }
    }
    out_nodes.clear();
    out_trans.clear();
    out_nodes.resize(order.size());
    for (size_t i = 0; i < order.size(); ++i) {
      const MutNode& mn = nodes_[order[i]];
      DawgNode dn;
      dn.is_final = mn.is_final ? 1u : 0u;
      dn.word_count = WordCount(order[i], word_memo);
      dn.trans_first = static_cast<uint32_t>(out_trans.size());
      dn.trans_count = static_cast<uint32_t>(mn.edges.size());
      out_nodes[i] = dn;
      for (const auto& [label, child] : mn.edges) {
        DawgTrans dt;
        dt.label = static_cast<uint8_t>(label);
        dt.target = dense[child];
        out_trans.push_back(dt);
      }
    }
    out_root = dense[root];
  }

  std::vector<MutNode> nodes_;
  std::unordered_map<std::string, int> register_;
  std::vector<int> active_path_ = {0};  // active_path_[d] = node after d chars.
  std::string previous_;
};

}  // namespace

bool BuildDawg(const std::vector<std::string>& keys,
               std::vector<DawgNode>& out_nodes,
               std::vector<DawgTrans>& out_trans, uint32_t& out_root) {
  if (keys.empty()) return false;
  Minimiser m;
  for (const std::string& key : keys) m.Add(key);
  m.Finish(out_nodes, out_trans, out_root);
  return true;
}

}  // namespace paiza
