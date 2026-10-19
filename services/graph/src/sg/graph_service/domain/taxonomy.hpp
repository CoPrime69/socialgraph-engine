#pragma once

/// @file taxonomy.hpp
/// The data-driven category/branch taxonomy (contracts §10.3).

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace sg::graph_service::domain {

struct Category {
  std::string slug;
  std::string name;
  std::string description;
};

struct Branch {
  std::string name;
  std::string category_slug;
};

/// Categories and branches in display order (ORDER BY id).
struct Taxonomy {
  std::vector<Category> categories;
  std::vector<Branch> branches;

  [[nodiscard]] bool empty() const noexcept { return categories.empty() || branches.empty(); }
  [[nodiscard]] bool has_category(std::string_view name) const noexcept { return category_code(name).has_value(); }
  [[nodiscard]] bool has_branch(std::string_view name) const noexcept { return branch_code(name).has_value(); }

  /// 0-based display-order index of the category named `name` (the sg::graph::Profile code).
  [[nodiscard]] std::optional<std::uint16_t> category_code(std::string_view name) const noexcept;
  /// 0-based display-order index of the branch named `name`.
  [[nodiscard]] std::optional<std::uint16_t> branch_code(std::string_view name) const noexcept;
  /// Name of the category with `slug`, if any.
  [[nodiscard]] std::optional<std::string> category_name_by_slug(std::string_view slug) const;
};

/// `{ "categories": [names], "branches": [names], "branch_categories": { branch: category name } }`.
[[nodiscard]] nlohmann::json to_json(const Taxonomy& taxonomy);

}  // namespace sg::graph_service::domain
