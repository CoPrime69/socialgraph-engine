#pragma once

/// @file centrality.hpp
/// Influence measures: PageRank, Brandes betweenness, closeness and degree centrality.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "sg/graph/csr_graph.hpp"

namespace sg::graph {

/// Options of pagerank().
struct PageRankOptions {
  double damping = 0.85;     ///< Probability of following an edge, in [0, 1].
  double tolerance = 1e-10;  ///< Stop when the L1 change between iterations is below this (>= 0).
  int max_iterations = 200;  ///< Upper bound on power iterations (>= 1).
};

/// Result of pagerank().
struct PageRankResult {
  std::vector<double> scores;  ///< One score per node; sums to 1 (empty graph: empty).
  int iterations = 0;          ///< Power iterations performed.
  bool converged = false;      ///< True when the L1 change fell below the tolerance.
};

/// PageRank of an undirected, unweighted graph by power iteration.
///
/// x' = (1 − d)/n + d·(Σ_{u ∈ N(v)} x[u]/deg(u) + Σ_{dangling u} x[u]/n): the mass of nodes
/// without edges is spread uniformly. Iterates until ‖x' − x‖₁ < tolerance or max_iterations;
/// the returned vector is renormalised to sum to exactly 1 (up to rounding). Single-threaded and
/// fully deterministic. Edge weights are ignored.
///
/// @throws std::invalid_argument on damping ∉ [0, 1], tolerance < 0 / NaN or max_iterations < 1.
/// Complexity: O(k·(V + E)) time for k iterations, O(V) extra memory.
[[nodiscard]] PageRankResult pagerank(const CsrGraph& g, const PageRankOptions& opts = {});

/// Options of betweenness().
struct BetweennessOptions {
  unsigned threads = 0;            ///< Worker threads; 0 = std::thread::hardware_concurrency().
  std::size_t sample_sources = 0;  ///< k > 0 && k < n: estimate from k pivot sources.
  std::uint64_t seed = 1;          ///< Seed of the deterministic pivot sampler.
  bool normalized = true;          ///< Divide by (n − 1)(n − 2)/2 (when n > 2).
};

/// Result of betweenness().
struct BetweennessResult {
  std::vector<double> scores;    ///< One score per node.
  bool approximate = false;      ///< True when pivot sampling was used.
  std::size_t sources_used = 0;  ///< Number of single-source passes accumulated.
};

/// Betweenness centrality (Brandes 2001) of an undirected, unweighted graph.
///
/// Each unordered pair {s, t} is counted once (the directed sums are halved). With
/// `normalized`, scores are divided by (n − 1)(n − 2)/2, the number of pairs not containing the
/// node, so a star centre scores 1. Graphs with n ≤ 2 score 0 everywhere.
///
/// Sampling: when 0 < sample_sources < n, k distinct pivots are drawn with SplitMix64(seed)
/// (partial Fisher-Yates) and the accumulated dependencies are scaled by n/k (unbiased
/// estimator; Brandes & Pich 2007). Otherwise the result is exact and `sources_used` = n.
///
/// Determinism: sources are cut into fixed blocks whose size depends only on the number of
/// sources; each block accumulates its sources in ascending order into its own buffer and the
/// buffers are added to the total strictly in block order. The result is therefore bit-identical
/// for every thread count. Each worker reuses one scratch area (dist/σ/δ/BFS order) for all its
/// sources; the backward pass finds predecessors by `dist[v] == dist[w] − 1` instead of storing
/// predecessor lists.
///
/// Threads: `threads == 0` uses the hardware concurrency, reduced for graphs so small that
/// threading costs more than it saves; an explicit count is honoured (capped by the number of
/// blocks). Edge weights are ignored.
///
/// Complexity: O(k·(V + E)) time for k sources (exact: O(V·E)), O(t·V) scratch for t threads
/// plus O(b·V) for the in-flight block buffers.
[[nodiscard]] BetweennessResult betweenness(const CsrGraph& g, const BetweennessOptions& opts = {});

/// Closeness centrality with the Wasserman-Faust correction for disconnected graphs:
/// C(u) = (r / D) · (r / (n − 1)), where r is the number of nodes reachable from u (excluding u)
/// and D the sum of their BFS distances. Equals (n − 1)/D on connected graphs; isolated nodes and
/// graphs with n ≤ 1 score 0. Never infinite.
///
/// Implemented with a bit-parallel multi-source BFS (64 sources per sweep, distances summed with
/// bit-sliced counters), parallel over source batches; results do not depend on `threads`
/// (0 = hardware concurrency). Edge weights are ignored.
///
/// Complexity: O(⌈V/64⌉ · L · (V + E)) word operations, where L ≤ diameter + 1 is the number of
/// BFS levels of a batch (O(V·E) worst case), O(t·V) extra memory for t threads.
[[nodiscard]] std::vector<double> closeness(const CsrGraph& g, unsigned threads = 0);

/// Degree centrality deg(u)/(n − 1); all zeros when n ≤ 1. Complexity: O(V).
[[nodiscard]] std::vector<double> degree_centrality(const CsrGraph& g);

}  // namespace sg::graph
