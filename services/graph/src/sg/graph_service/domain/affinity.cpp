#include "sg/graph_service/domain/affinity.hpp"

#include <stdexcept>
#include <string>

namespace sg::graph_service::domain {

sg::graph::Profile to_graph_profile(const UserProfile& profile, const Taxonomy& taxonomy) {
  const auto category = taxonomy.category_code(profile.category);
  const auto branch = taxonomy.branch_code(profile.branch);
  if (!category || !branch) {
    throw std::invalid_argument("profile " + std::to_string(profile.id) + " has a category or branch outside the taxonomy");
  }
  return sg::graph::Profile{*category, *branch, profile.influence};
}

double member_connection_strength(const UserProfile& a, const UserProfile& b, const Taxonomy& taxonomy) {
  return sg::graph::connection_strength(to_graph_profile(a, taxonomy), to_graph_profile(b, taxonomy));
}

}  // namespace sg::graph_service::domain
