//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include "lgraph_base_core.hpp"
#include "mmap_tree.hpp"
#include "node.hpp"

using Tree_index = mmap_lib::Tree_index;

class Node_tree : public mmap_lib::tree<Node> {
private:
  constexpr static bool debug_verbose = false;

protected:
  LGraph *root;

  // store last tree index written for each component type (costs a bit to set up, but drops traversal time from O(n^2) -> O(n))
  absl::flat_hash_map<Tree_index, std::array<Tree_index, 24>> last_free;

public:
  Node_tree(LGraph *root);

  // return root LGraph used to generate the node tree
  LGraph *get_root_lg() const { return root; }

  Tree_index get_last_free(Tree_index tidx, Ntype_op op) { return last_free[tidx][size_t(op) - 1]; }
  void set_last_free(Tree_index tidx, Ntype_op op, Tree_index new_tidx) { last_free[tidx][size_t(op) - 1] = new_tidx; }

  void dump() const;
};