#include "planner.h"

#include <cassert>
#include <cstdlib>
#include <random>
#include <vector>

// Keep checks active in Release builds, where the standard assert is disabled.
#undef assert
#define assert(expression) do { if (!(expression)) std::abort(); } while (false)

using layer_pack::Layer;
using layer_pack::Scope;
using layer_pack::make_plan;

int main() {
  {
    // Ordinary gaps disappear, preserving the order of occupied layers.
    auto p = make_plan({{true, false}, {}, {true, false}, {}, {true, false}}, {});
    assert((p.destination == std::vector<int>{0, -1, 1, -1, 2}));
  }
  {
    // The two original targets remain the only members of the group scope.
    auto p = make_plan({{true, false}, {}, {true, false}, {}, {true, false},
                        {}, {true, false}}, {{0, 4}});
    assert((p.destination == std::vector<int>{0, -1, 1, -1, 2, -1, 3}));
    assert(p.scope_count[0] == 2);
  }
  {
    // A finite empty camera scope needs one blank fence before an outsider.
    auto p = make_plan({{true, false}, {}, {}, {true, false}}, {{0, 2}});
    assert((p.destination == std::vector<int>{0, 1, -1, 2}));
    assert(p.scope_count[0] == 1);
    assert(p.retained_guards == 1);
  }
  {
    // Unlimited scope remains unlimited.
    auto p = make_plan({{true, false}, {}, {true, false}}, {{0, 0}});
    assert(p.scope_count[0] == 0);
  }
  {
    // Layer settings are pinned instead of being silently transferred.
    auto p = make_plan({{true, false}, {}, {false, true}, {}, {true, false}}, {});
    assert((p.destination == std::vector<int>{0, -1, 2, -1, 3}));
  }
  {
    // Overlapping camera/group scopes each get their own adjusted count.
    auto p = make_plan({{true, false}, {}, {true, false}, {},
                        {true, false}, {}, {true, false}}, {{0, 4}, {2, 4}});
    assert(p.scope_count[0] == 2);
    assert(p.scope_count[1] == 2);
  }
  {
    std::mt19937 random(0x6C617965);
    for (int trial = 0; trial < 2000; ++trial) {
      const int n = 2 + static_cast<int>(random() % 40);
      std::vector<Layer> layers(n);
      for (auto& layer : layers) {
        layer.occupied = random() % 3 == 0;
        layer.protected_layer = random() % 13 == 0;
      }
      std::vector<Scope> scopes;
      for (int i = 0; i < n; ++i)
        if (layers[i].occupied && random() % 4 == 0)
          scopes.push_back({i, static_cast<int>(random() % (n + 1))});
      auto p = make_plan(layers, scopes);
      int previous = -1;
      for (int i = 0; i < n; ++i) {
        if (!layers[i].occupied) continue;
        assert(p.destination[i] > previous);
        if (layers[i].protected_layer) assert(p.destination[i] == i);
        previous = p.destination[i];
      }
      for (size_t s = 0; s < scopes.size(); ++s) {
        const auto& scope = scopes[s];
        for (int i = scope.layer + 1; i < n; ++i) {
          if (!layers[i].occupied) continue;
          const bool before = scope.count == 0 || i - scope.layer <= scope.count;
          const bool after = p.scope_count[s] == 0 ||
              p.destination[i] - p.destination[scope.layer] <= p.scope_count[s];
          assert(before == after);
        }
      }
    }
  }
}
