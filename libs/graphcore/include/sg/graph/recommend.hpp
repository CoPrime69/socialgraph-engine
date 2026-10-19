#pragma once

/// @file recommend.hpp
/// Connection recommendations: friends-of-friends structure (Adamic–Adar) + attribute affinity.

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "sg/graph/affinity.hpp"
#include "sg/graph/csr_graph.hpp"

namespace sg::graph {

/// Options of recommend(). The weights default to contracts.md §8.
struct RecommendOptions {
  std::size_t limit = 10;      ///< Maximum number of recommendations returned.
  double w_structural = 0.55;  ///< Weight of the normalised Adamic–Adar index.
  double w_affinity = 0.35;    ///< Weight of affinity().
  double w_community = 0.10;   ///< Weight of [same community].
};

/// One recommended user and the explanation of its score.
struct Recommendation {
  NodeId node;           ///< Recommended node.
  double score;          ///< w_s·AA/max AA + w_a·affinity + w_c·[same community].
  std::uint32_t mutual;  ///< Number of common neighbours with the user.
  double adamic_adar;    ///< Σ over common neighbours z of 1 / ln(deg z) (raw, not normalised).
  double affinity;       ///< affinity(user, node), unrounded.
  bool same_category;
  bool same_branch;
  bool same_community;   ///< False when no membership was supplied.
};

/// Recommends new connections for `user` (contracts.md §8).
///
/// Every node v ≠ user that is not already a neighbour is scored
/// `w_structural·AA(u,v)/max AA + w_affinity·affinity(u,v) + w_community·[same community]`,
/// where max AA is taken over all candidates; when no candidate shares a neighbour with the user
/// (e.g. a new user without connections) the structural term is 0 and the ranking falls back to
/// attribute affinity (plus community). `membership` may be empty (no community term). The
/// `limit` best candidates are returned by score descending, ties broken by smaller node id.
///
/// The Adamic–Adar sums live in a per-thread scratch area that grows to the largest graph queried
/// on that thread; a query resets only the entries it touched, so repeated queries allocate
/// nothing proportional to V (only the returned vector and a heap of `limit` entries).
///
/// @throws std::invalid_argument if profiles.size() != n, membership is neither empty nor of size
///         n, or a weight is negative / not finite.
/// @throws std::out_of_range if user >= n.
/// Complexity: O(V log limit + Σ_{z ∈ N(user)} deg z) time; O(V) per-thread scratch, reused.
[[nodiscard]] std::vector<Recommendation> recommend(const CsrGraph& g,
                                                    std::span<const Profile> profiles,
                                                    std::span<const std::uint32_t> membership,
                                                    NodeId user, const RecommendOptions& opts = {});

}  // namespace sg::graph
