#pragma once

/// @file components.hpp
/// Connected components. Also available through community.hpp, paths.hpp and graphcore.hpp.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "sg/graph/csr_graph.hpp"

namespace sg::graph {

/// Result of connected_components().
struct ComponentsResult {
  /// Component id of every node. Ids are 0..count−1, numbered in order of each component's
  /// smallest node id (the component of node 0 is 0).
  std::vector<std::uint32_t> component;
  std::size_t count = 0;    ///< Number of components (isolated nodes are components of size 1).
  std::size_t largest = 0;  ///< Size of the largest component (0 for an empty graph).
};

/// Connected components by breadth-first search. Complexity: O(V + E) time, O(V) memory.
[[nodiscard]] ComponentsResult connected_components(const CsrGraph& g);

}  // namespace sg::graph
