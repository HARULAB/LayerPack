#include "planner.h"

#include <algorithm>
#include <stdexcept>

namespace layer_pack {

Plan make_plan(const std::vector<Layer>& layers, const std::vector<Scope>& scopes) {
  const int size = static_cast<int>(layers.size());
  Plan plan;
  plan.destination.assign(size, -1);
  plan.scope_count.reserve(scopes.size());
  std::vector<bool> keep(size, false);
  for (int i = 0; i < size; ++i)
    keep[i] = layers[i].occupied || layers[i].protected_layer;

  // A finite scope containing no layer must not swallow the first layer below
  // it after compaction. Preserve one otherwise-empty fence in that case.
  for (const auto& scope : scopes) {
    if (scope.layer < 0 || scope.layer >= size || scope.count < 0)
      throw std::invalid_argument("invalid scope");
    if (scope.count == 0) continue;
    const int end = static_cast<int>(std::min<long long>(size - 1, static_cast<long long>(scope.layer) + scope.count));
    bool inside = false, below = false;
    for (int i = scope.layer + 1; i <= end; ++i)
      inside |= layers[i].occupied || layers[i].protected_layer;
    for (int i = end + 1; i < size; ++i)
      below |= layers[i].occupied || layers[i].protected_layer;
    if (!inside && below && scope.layer + 1 < size && !keep[scope.layer + 1]) {
      keep[scope.layer + 1] = true;
      ++plan.retained_guards;
    }
  }

  int next = 0;
  for (int i = 0; i < size; ++i) {
    if (!keep[i]) continue;
    // Named, hidden, or locked layers remain at their original positions.
    if (layers[i].protected_layer) next = std::max(next, i);
    plan.destination[i] = next++;
    if (layers[i].occupied && plan.destination[i] != i) ++plan.moved_layers;
  }

  for (const auto& scope : scopes) {
    if (scope.count == 0) {
      plan.scope_count.push_back(0);
      continue;
    }
    const int end = static_cast<int>(std::min<long long>(size - 1, static_cast<long long>(scope.layer) + scope.count));
    int last = -1;
    for (int i = scope.layer + 1; i <= end; ++i)
      if (plan.destination[i] >= 0) last = plan.destination[i];
    if (last >= 0) {
      plan.scope_count.push_back(last - plan.destination[scope.layer]);
    } else {
      // Nothing lower can newly enter this range; preserve the user's setting.
      plan.scope_count.push_back(scope.count);
    }
  }
  return plan;
}

} // namespace layer_pack
