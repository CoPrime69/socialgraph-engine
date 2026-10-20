#pragma once

/// @file generator.hpp
/// Portable pseudo-random numbers and the deterministic homophilous seed-graph generator.

#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "sg/graph/csr_graph.hpp"

namespace sg::graph {

/// SplitMix64 (Steele, Lea & Flood 2014; reference code by S. Vigna). Pure 64-bit integer
/// arithmetic, so the sequence is identical on every platform and compiler. Not cryptographic.
class SplitMix64 {
 public:
  /// Generator whose first output is splitmix64(seed + golden gamma). Complexity: O(1).
  explicit SplitMix64(std::uint64_t seed) noexcept : state_(seed) {}

  /// Next 64 uniformly distributed bits. Complexity: O(1).
  std::uint64_t next() noexcept {
    std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31U);
  }

  /// Uniform integer in [0, n) without modulo bias (Lemire 2019, multiply-and-reject).
  /// @throws std::invalid_argument if n == 0. Complexity: O(1) expected (< 2 draws).
  std::uint64_t bounded(std::uint64_t n);

  /// Uniform double in [0, 1) with 53 random bits. Complexity: O(1).
  double uniform01() noexcept {
    return static_cast<double>(next() >> 11U) * 0x1.0p-53;
  }

 private:
  std::uint64_t state_;
};

/// Options of generate_homophilous_edges(); defaults are the contract's seed-graph parameters.
struct GeneratorOptions {
  double average_degree = 8.0;  ///< Each node draws round(average_degree / 2) partners (>= 0).
  double mixing = 0.2;          ///< μ: probability of a partner outside the group, in [0, 1].
  std::uint64_t seed = 20241109;
};

/// Homophilous random graph used to seed the demo network (contracts.md §8).
///
/// Nodes 0..n−1 are processed in order; each draws round(average_degree/2) partners (half away
/// from zero). For every draw r = uniform01(): if r < 1 − μ and the node's group has more than one
/// member, the partner is a uniformly random *other* member of its group (members ordered by node
/// id, j = bounded(size − 1), skipping the node's own position); otherwise a uniformly random
/// other node (j = bounded(n − 1), skipping the node itself). The pairs are returned as
/// (min, max), sorted ascending and deduplicated, so the realised average degree is slightly
/// below `average_degree`. Group ids are arbitrary 32-bit values compared for equality.
/// Bit-identical on every platform (SplitMix64, no std::*_distribution).
///
/// @throws std::invalid_argument on average_degree < 0 / not finite, mixing ∉ [0, 1] or more
///         than CsrGraph::kMaxNodes nodes.
/// Complexity: O(V log V + V·average_degree·log(V·average_degree)).
[[nodiscard]] std::vector<std::pair<NodeId, NodeId>> generate_homophilous_edges(
    std::span<const std::uint32_t> group_of_node, const GeneratorOptions& opts = {});

}  // namespace sg::graph
