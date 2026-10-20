#pragma once

/// @file paths.hpp
/// Unweighted shortest paths ("degrees of separation") and BFS distances.

#include <cstdint>
#include <optional>
#include <vector>

#include "sg/graph/components.hpp"
#include "sg/graph/csr_graph.hpp"

namespace sg::graph {

/// A shortest path from `from` to `to` (both included) by bidirectional breadth-first search,
/// always expanding the smaller frontier by one full level.
///
/// Returns {from} when from == to and std::nullopt when `to` is unreachable. Among several
/// shortest paths the one found first in ascending neighbour order is returned (deterministic).
/// Uses a per-thread scratch area (stamped, never cleared) so repeated queries on the same graph
/// cost O(visited) rather than O(V); the scratch grows to the largest graph queried on that
/// thread. Edge weights are ignored.
///
/// @throws std::out_of_range if `from` or `to` >= g.node_count().
/// Complexity: O(b^(d/2)) expected for branching factor b and distance d; O(V + E) worst case.
[[nodiscard]] std::optional<std::vector<NodeId>> shortest_path(const CsrGraph& g, NodeId from,
                                                               NodeId to);

/// Hop distance from `source` to every node; -1 for unreachable nodes.
/// @throws std::out_of_range if source >= g.node_count(). Complexity: O(V + E).
[[nodiscard]] std::vector<std::int32_t> bfs_distances(const CsrGraph& g, NodeId source);

}  // namespace sg::graph
