#pragma once

/// @file csr_graph.hpp
/// Immutable undirected graph stored in compressed sparse row (CSR) form.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace sg::graph {

/// Dense vertex identifier. Valid ids of a graph are `0 .. node_count() - 1`.
using NodeId = std::uint32_t;

/// Value that is never a valid node id (used as a sentinel by the algorithms).
inline constexpr NodeId kInvalidNode = std::numeric_limits<NodeId>::max();

/// One undirected weighted edge, the input of CsrGraph::from_edges.
struct WeightedEdge {
  NodeId u;
  NodeId v;
  double w;
};

/// Immutable, undirected, weighted graph in compressed sparse row form.
///
/// Every undirected edge {u, v} is stored twice (as u -> v and v -> u) in one contiguous
/// `adjacency()` array; the neighbours of `u` are the slice
/// `adjacency()[offsets()[u] .. offsets()[u + 1])`, sorted ascending, without duplicates and
/// without self-loops. `weights(u)` is the parallel slice of edge weights, so the weight of
/// {u, v} is identical in both directions.
///
/// The object is a value type: cheap to move, safe to share between threads for reading (no
/// member mutates state after construction).
class CsrGraph {
 public:
  /// Largest supported node count; `kInvalidNode` stays reserved as a sentinel.
  static constexpr std::size_t kMaxNodes = std::numeric_limits<NodeId>::max();

  /// Empty graph (0 nodes, 0 edges).
  CsrGraph() = default;

  /// Builds a graph with `node_count` nodes from an undirected edge list.
  ///
  /// * Self-loops (u == v) are dropped.
  /// * Duplicate pairs, in either orientation, are merged; the weight of the first occurrence
  ///   in input order is kept.
  /// * Neighbour lists are sorted ascending.
  ///
  /// @throws std::invalid_argument if an endpoint is >= node_count, if a weight is not finite,
  ///         or if node_count > kMaxNodes.
  /// Complexity: O(V + E log d_max) time, O(V + E) extra memory (counting sort by source, then a
  /// small sort per neighbour list).
  [[nodiscard]] static CsrGraph from_edges(std::size_t node_count,
                                           std::span<const WeightedEdge> edges);

  /// Same as from_edges with every weight equal to 1.0.
  /// Complexity: O(V + E log d_max).
  [[nodiscard]] static CsrGraph from_pairs(std::size_t node_count,
                                           std::span<const std::pair<NodeId, NodeId>> edges);

  /// Number of nodes. Complexity: O(1).
  [[nodiscard]] std::size_t node_count() const noexcept {
    return offsets_.empty() ? 0 : offsets_.size() - 1;
  }

  /// Number of undirected edges (each {u, v} counted once). Complexity: O(1).
  [[nodiscard]] std::size_t edge_count() const noexcept { return targets_.size() / 2; }

  /// Sorted neighbours of `u`. @throws std::out_of_range if u >= node_count(). Complexity: O(1).
  [[nodiscard]] std::span<const NodeId> neighbors(NodeId u) const {
    check_node(u);
    return {targets_.data() + offsets_[u], targets_.data() + offsets_[u + 1]};
  }

  /// Weights parallel to neighbors(u). @throws std::out_of_range if u >= node_count().
  /// Complexity: O(1).
  [[nodiscard]] std::span<const double> weights(NodeId u) const {
    check_node(u);
    return {weights_.data() + offsets_[u], weights_.data() + offsets_[u + 1]};
  }

  /// Number of neighbours of `u`. @throws std::out_of_range if u >= node_count().
  /// Complexity: O(1).
  [[nodiscard]] std::size_t degree(NodeId u) const {
    check_node(u);
    return offsets_[u + 1] - offsets_[u];
  }

  /// True when the undirected edge {u, v} exists (always false for u == v).
  /// @throws std::out_of_range if u or v >= node_count(). Complexity: O(log degree(u)).
  [[nodiscard]] bool has_edge(NodeId u, NodeId v) const;

  /// Weight of edge {u, v}, or std::nullopt if absent.
  /// @throws std::out_of_range if u or v >= node_count(). Complexity: O(log degree(u)).
  [[nodiscard]] std::optional<double> edge_weight(NodeId u, NodeId v) const;

  /// Sum of the weights of all undirected edges (each edge counted once). Complexity: O(1).
  [[nodiscard]] double total_weight() const noexcept { return total_weight_; }

  /// Largest degree in the graph (0 for an empty graph). Complexity: O(1).
  [[nodiscard]] std::size_t max_degree() const noexcept { return max_degree_; }

  /// Raw CSR row offsets (`node_count() + 1` entries, or none for an empty graph).
  /// Complexity: O(1).
  [[nodiscard]] std::span<const std::size_t> offsets() const noexcept { return offsets_; }

  /// Raw concatenated neighbour lists (2 * edge_count() entries). Complexity: O(1).
  [[nodiscard]] std::span<const NodeId> adjacency() const noexcept { return targets_; }

  /// Raw weights parallel to adjacency(). Complexity: O(1).
  [[nodiscard]] std::span<const double> adjacency_weights() const noexcept { return weights_; }

  /// Every undirected edge once as {u, v, w} with u < v, sorted by (u, v).
  /// Complexity: O(V + E).
  [[nodiscard]] std::vector<WeightedEdge> edge_list() const;

  /// Copy of this graph whose edge weights are `fn(old_weight)` (e.g. louvain_weight).
  /// @throws std::invalid_argument if `fn` returns a non-finite value. Complexity: O(V + E).
  template <class Fn>
  [[nodiscard]] CsrGraph map_weights(Fn&& fn) const {
    CsrGraph out = *this;
    double total = 0.0;
    for (std::size_t u = 0; u + 1 < out.offsets_.size(); ++u) {
      for (std::size_t i = out.offsets_[u]; i < out.offsets_[u + 1]; ++i) {
        const double w = static_cast<double>(fn(out.weights_[i]));
        if (!std::isfinite(w)) {
          throw std::invalid_argument("CsrGraph::map_weights: weight function returned a non-finite value");
        }
        out.weights_[i] = w;
        if (u < out.targets_[i]) {
          total += w;
        }
      }
    }
    out.total_weight_ = total;
    return out;
  }

  /// Structural and weight equality. Complexity: O(V + E).
  friend bool operator==(const CsrGraph&, const CsrGraph&) = default;

 private:
  void check_node(NodeId u) const {
    if (static_cast<std::size_t>(u) >= node_count()) {
      throw std::out_of_range("CsrGraph: node id out of range");
    }
  }

  template <class EdgeAt>
  static CsrGraph build(std::size_t node_count, std::size_t edge_input_count, EdgeAt edge_at);

  std::vector<std::size_t> offsets_;  // node_count + 1 entries (empty when node_count == 0)
  std::vector<NodeId> targets_;       // sorted neighbour lists, both directions of every edge
  std::vector<double> weights_;       // parallel to targets_
  double total_weight_ = 0.0;         // sum over undirected edges
  std::size_t max_degree_ = 0;
};

}  // namespace sg::graph
