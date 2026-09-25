#pragma once

#include <vector>

namespace layer_pack {

struct Layer {
  bool occupied = false;
  bool protected_layer = false;
};

struct Scope {
  int layer = 0;
  int count = 0; // 0 means all lower layers.
};

struct Plan {
  std::vector<int> destination; // -1 for unused source layers.
  std::vector<int> scope_count;
  int moved_layers = 0;
  int retained_guards = 0;
};

Plan make_plan(const std::vector<Layer>& layers, const std::vector<Scope>& scopes);

} // namespace layer_pack
