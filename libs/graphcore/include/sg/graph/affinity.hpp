#pragma once

/// @file affinity.hpp
/// Attribute affinity between two users (contracts.md §8).

#include <cstdint>

namespace sg::graph {

/// Attributes of a user that the domain formulas use. `category` and `branch` are dense codes
/// (e.g. the index in the shared taxonomy); `influence` is on the 0–100 scale.
struct Profile {
  std::uint16_t category;
  std::uint16_t branch;
  double influence;
};

/// Weight of "same category" in affinity().
inline constexpr double kAffinityCategoryWeight = 0.4;
/// Weight of "same branch" in affinity().
inline constexpr double kAffinityBranchWeight = 0.3;
/// Weight of influence similarity in affinity().
inline constexpr double kAffinityInfluenceWeight = 0.3;
/// Influence values live on [0, kInfluenceScale].
inline constexpr double kInfluenceScale = 100.0;

/// Affinity of two users in [0, 1] (contracts.md §8):
/// `0.4·[same category] + 0.3·[same branch] + 0.3·(1 − |Δinfluence| / 100)`.
/// The influence term is clamped to [0, 1], so out-of-scale influences cannot push the result
/// outside [0, 1]. Symmetric. Complexity: O(1).
[[nodiscard]] double affinity(const Profile& a, const Profile& b) noexcept;

/// Connection strength stored with a new connection: affinity() rounded to 3 decimals
/// (half away from zero). Complexity: O(1).
[[nodiscard]] double connection_strength(const Profile& a, const Profile& b) noexcept;

/// Louvain edge weight derived from a connection strength: `0.5 + 0.5·strength`
/// (structure dominates, attributes refine). Complexity: O(1).
[[nodiscard]] double louvain_weight(double strength) noexcept;

}  // namespace sg::graph
