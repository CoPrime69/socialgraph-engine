#pragma once

/// @file community.hpp
/// Community detection (Louvain modularity optimisation) and modularity.

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "sg/graph/components.hpp"
#include "sg/graph/csr_graph.hpp"

namespace sg::graph {

/// Options of louvain().
struct LouvainOptions {
  double resolution = 1.0;  ///< γ in the modularity objective (>= 0); larger γ → smaller communities.
  int max_levels = 10;      ///< Maximum number of aggregation levels (>= 1).
  int max_passes = 50;      ///< Maximum local-moving passes per level (>= 1).
  double min_gain = 1e-7;   ///< Stop a level / the hierarchy when modularity improves less (>= 0).
};

/// Result of louvain().
struct CommunityResult {
  /// Community of every node, renumbered 0..k−1 by community size (descending), ties broken by
  /// the smallest member id (ascending).
  std::vector<std::uint32_t> membership;
  std::size_t community_count = 0;  ///< k.
  double modularity = 0.0;          ///< Modularity of `membership` on the input graph.
  int levels = 0;                   ///< Aggregation levels that changed the partition.
};

/// Weighted Louvain community detection (Blondel et al. 2008), deterministic.
///
/// Local moving visits nodes in ascending id order; a node joins the neighbouring community with
/// the largest strictly positive modularity gain (ties keep the first candidate in neighbour
/// order, staying put wins all ties with zero gain). A level ends when a pass moves nothing or
/// improves modularity by less than `min_gain`; communities are then aggregated into nodes and
/// the process repeats until a level changes nothing, improves less than `min_gain`, or
/// `max_levels` is reached.
///
/// Weights: the CsrGraph edge weights (use louvain_weight(strength) for the contract's weights);
/// they must be >= 0. Self-loop convention: an aggregated node carries the total weight of the
/// edges inside its community as a self-loop of weight w_ii counted once in the total weight m
/// and twice in the node strength k_i (so Σ k_i = 2m), the usual convention of Newman's
/// modularity and of the reference Louvain implementation. The input CsrGraph never has
/// self-loops.
///
/// The reported modularity is recomputed from scratch on the original graph with modularity().
/// A graph without edges yields singletons, modularity 0 and 0 levels.
///
/// @throws std::invalid_argument on a negative edge weight or invalid options.
/// Complexity: O(p·(V + E)) per level for p passes; in practice ~O(E log V) overall. O(V + E)
/// memory.
[[nodiscard]] CommunityResult louvain(const CsrGraph& g, const LouvainOptions& opts = {});

/// Newman-Girvan modularity of a partition of a weighted graph:
/// Q = Σ_c [ L_c / m − γ · (K_c / 2m)² ], with L_c the weight of edges inside c, K_c the sum of
/// the weighted degrees in c and m the total edge weight. Community labels are arbitrary
/// 32-bit values. Returns 0 when the graph has no edge weight.
///
/// @throws std::invalid_argument if membership.size() != g.node_count().
/// Complexity: O(V + E) when labels are < 4V, otherwise O(V log V + E).
[[nodiscard]] double modularity(const CsrGraph& g, std::span<const std::uint32_t> membership,
                                double resolution = 1.0);

}  // namespace sg::graph
