#pragma once

// Graph builders, independent brute-force reference implementations and seed-data loading
// shared by the graphcore tests. The references deliberately use different data structures
// (dense matrices, Floyd-Warshall) from the library so they can catch its mistakes.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "sg/graph/graphcore.hpp"

namespace sg::graph::testing {

using Pairs = std::vector<std::pair<NodeId, NodeId>>;

// ---- builders -------------------------------------------------------------------------------
CsrGraph make_graph(std::size_t n, const Pairs& edges);
CsrGraph path_graph(std::size_t n);                           // 0-1-2-...-(n-1)
CsrGraph cycle_graph(std::size_t n);                          // path + (n-1, 0)
CsrGraph star_graph(std::size_t n);                           // centre 0, leaves 1..n-1
CsrGraph complete_graph(std::size_t n);
CsrGraph two_cliques_with_bridge(std::size_t clique_size);    // K_s on 0..s-1 and s..2s-1, bridge (s-1, s)
CsrGraph ring_of_cliques(std::size_t cliques, std::size_t clique_size);  // clique i linked to i+1
/// Erdős-Rényi G(n, p) drawn with SplitMix64(seed).
CsrGraph random_graph(std::size_t n, double p, std::uint64_t seed);

// ---- references -----------------------------------------------------------------------------
inline constexpr int kUnreachable = -1;

/// All-pairs hop distances by Floyd-Warshall on a dense matrix (-1 = unreachable).
std::vector<std::vector<int>> floyd_warshall(const CsrGraph& g);

/// PageRank by power iteration on the explicit dense Google matrix (dangling columns uniform).
std::vector<double> dense_pagerank(const CsrGraph& g, double damping, int iterations);

/// Betweenness by brute force: for every pair s < t and every v, add σ_sv·σ_vt/σ_st when v lies on
/// a shortest s-t path. Path counts σ come from a DP over the Floyd-Warshall distances.
std::vector<double> brute_force_betweenness(const CsrGraph& g, bool normalized);

/// Wasserman-Faust closeness from Floyd-Warshall distances.
std::vector<double> reference_closeness(const CsrGraph& g);

/// Q = 1/(2m) Σ_ij [A_ij − γ k_i k_j / 2m] δ(c_i, c_j) on the dense weighted adjacency matrix.
double dense_modularity(const CsrGraph& g, const std::vector<std::uint32_t>& membership,
                        double resolution = 1.0);

// ---- seed data (the original 100 users, contracts.md §8) ------------------------------------
struct SeedUser {
  std::string handle;
  std::string category;
  std::string branch;
  double influence = 0.0;
};

/// The embedded copy of the original seed users (seed_fixture.hpp).
std::vector<SeedUser> seed_users();
/// The first 100 rows of legacy/user_data.csv (path compiled in as SG_LEGACY_USERS_CSV), the
/// original source of the seed users; std::nullopt when the file is not present. Only used to
/// cross-check the embedded copy.
std::optional<std::vector<SeedUser>> load_legacy_seed_users();
/// Profile codes = index in the original taxonomy (contracts.md §3).
std::vector<Profile> seed_profiles(const std::vector<SeedUser>& users);
/// Group of each user = (category, branch) code.
std::vector<std::uint32_t> seed_groups(const std::vector<SeedUser>& users);

}  // namespace sg::graph::testing
