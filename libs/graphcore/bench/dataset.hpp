#pragma once

// Benchmark-only data loading (the library itself does no I/O): the real Stack Overflow 2008
// interaction graph of data/stackoverflow (contracts.md §10) and the synthetic graphs.

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "sg/graph/graphcore.hpp"

namespace sg::bench {

/// A graph prepared for benchmarking, with the attributes the algorithms need.
struct BenchGraph {
  std::string name;
  graph::CsrGraph graph;                 ///< weights = connection strength (synthetic: affinity)
  std::vector<graph::Profile> profiles;  ///< one per node
  std::vector<std::string> external_ids; ///< string id per node, as the legacy code keyed users
  std::vector<std::string> categories;   ///< category name per node (legacy User field)
  std::vector<std::string> branches;     ///< branch name per node (legacy User field)
  std::vector<graph::WeightedEdge> edges; ///< the input edge list (for CSR build benchmarks)
};

/// Directory of the real dataset: $SG_DATASET_DIR, else the path compiled in as SG_DATASET_DIR.
std::string dataset_dir();

/// True when this binary can read gzip files (built with zlib).
bool has_zlib();

/// Loads users.csv(.gz) and connections.csv(.gz) from `dir` (gzip preferred, plain CSV as a
/// fallback). Returns std::nullopt with `error` set when the files are missing or unreadable.
std::optional<BenchGraph> load_stackoverflow(const std::string& dir, std::string& error);

/// Synthetic homophilous graph: n nodes in groups of ~32, generator with average degree 10,
/// μ = 0.2; profiles drawn with SplitMix64. Deterministic.
BenchGraph make_synthetic(std::size_t n);

}  // namespace sg::bench
