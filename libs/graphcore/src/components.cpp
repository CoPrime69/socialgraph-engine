#include "sg/graph/components.hpp"

#include <algorithm>
#include <limits>

namespace sg::graph {

ComponentsResult connected_components(const CsrGraph& g) {
  constexpr std::uint32_t kUnassigned = std::numeric_limits<std::uint32_t>::max();
  const std::size_t n = g.node_count();
  ComponentsResult result;
  result.component.assign(n, kUnassigned);
  if (n == 0) {
    return result;
  }
  const std::size_t* offsets = g.offsets().data();
  const NodeId* adjacency = g.adjacency().data();
  std::vector<NodeId> queue(n);
  std::uint32_t label = 0;
  for (std::size_t s = 0; s < n; ++s) {
    if (result.component[s] != kUnassigned) {
      continue;
    }
    std::size_t head = 0;
    std::size_t tail = 0;
    queue[tail++] = static_cast<NodeId>(s);
    result.component[s] = label;
    while (head < tail) {
      const NodeId v = queue[head++];
      for (std::size_t i = offsets[v]; i < offsets[v + 1]; ++i) {
        const NodeId w = adjacency[i];
        if (result.component[w] == kUnassigned) {
          result.component[w] = label;
          queue[tail++] = w;
        }
      }
    }
    result.largest = std::max(result.largest, tail);
    ++label;
  }
  result.count = label;
  return result;
}

}  // namespace sg::graph
