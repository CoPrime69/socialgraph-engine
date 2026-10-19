#pragma once

/// @file taxonomy.hpp
/// The category/branch taxonomy as identity-service sees it. graph-service owns it (data-driven,
/// contracts §10.3); identity caches a copy to validate registrations.

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace sg::identity::domain {

struct Taxonomy {
  std::vector<std::string> categories;  ///< names, display order
  std::vector<std::string> branches;    ///< names, display order

  [[nodiscard]] bool has_category(std::string_view name) const {
    return std::find(categories.begin(), categories.end(), name) != categories.end();
  }
  [[nodiscard]] bool has_branch(std::string_view name) const {
    return std::find(branches.begin(), branches.end(), name) != branches.end();
  }
  /// An empty taxonomy cannot validate anything (graph-service not seeded yet).
  [[nodiscard]] bool usable() const noexcept { return !categories.empty() && !branches.empty(); }
};

}  // namespace sg::identity::domain
