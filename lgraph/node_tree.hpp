//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include "lgraph_base_core.hpp"
#include "tree.hpp"
#include "node.hpp"

class Node_tree : public hhds::tree<Node> {
private:
  constexpr static bool debug_verbose = false;

protected:
  Lgraph *root;

  // store last tree index written for each component type (costs a bit to set up, but drops write time from O(n^2) -> O(n))
  // TODO: find a way of determining the number of synth types in an lgraph
  absl::flat_hash_map<hhds::Tree_pos, std::array<hhds::Tree_pos, 25>> last_free;

public:
  // do not copy node trees if possible, very slow
  Node_tree(const Node_tree &other) = delete;
  Node_tree(Node_tree &&other)      = default;
  Node_tree(Lgraph *root);

  // return root Lgraph used to generate the node tree
  Lgraph *get_root_lg() const { return root; }

  hhds::Tree_pos get_last_free(hhds::Tree_pos pos, Ntype_op op) { return last_free[pos][size_t(op) - 1]; }
  void set_last_child(hhds::Tree_pos pos, Ntype_op op, hhds::Tree_pos new_pos) { last_free[pos][size_t(op) - 1] = new_pos; }

  void dump() const;
};
