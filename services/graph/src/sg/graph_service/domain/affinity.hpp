#pragma once

/// @file affinity.hpp
/// Connection strength of member-created connections (contracts §8, §10.5), via libs/graphcore.

#include <sg/graph/affinity.hpp>

#include "sg/graph_service/domain/profile.hpp"
#include "sg/graph_service/domain/taxonomy.hpp"

namespace sg::graph_service::domain {

/// The graphcore view of a profile: category/branch codes are 0-based display-order indexes.
/// @throws std::invalid_argument when the profile's category or branch is not in `taxonomy`.
[[nodiscard]] sg::graph::Profile to_graph_profile(const UserProfile& profile, const Taxonomy& taxonomy);

/// Strength of a new member-created connection: affinity rounded to 3 decimals.
/// @throws std::invalid_argument as to_graph_profile().
[[nodiscard]] double member_connection_strength(const UserProfile& a, const UserProfile& b, const Taxonomy& taxonomy);

}  // namespace sg::graph_service::domain
