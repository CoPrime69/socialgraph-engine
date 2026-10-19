#include "sg/graph_service/domain/taxonomy.hpp"

#include <limits>

namespace sg::graph_service::domain {
namespace {

template <class Items, class Name>
std::optional<std::uint16_t> index_of(const Items& items, std::string_view name, Name get_name) noexcept {
  for (std::size_t i = 0; i < items.size() && i <= std::numeric_limits<std::uint16_t>::max(); ++i) {
    if (get_name(items[i]) == name) {
      return static_cast<std::uint16_t>(i);
    }
  }
  return std::nullopt;
}

}  // namespace

std::optional<std::uint16_t> Taxonomy::category_code(std::string_view name) const noexcept {
  return index_of(categories, name, [](const Category& c) -> const std::string& { return c.name; });
}

std::optional<std::uint16_t> Taxonomy::branch_code(std::string_view name) const noexcept {
  return index_of(branches, name, [](const Branch& b) -> const std::string& { return b.name; });
}

std::optional<std::string> Taxonomy::category_name_by_slug(std::string_view slug) const {
  for (const auto& c : categories) {
    if (c.slug == slug) {
      return c.name;
    }
  }
  return std::nullopt;
}

nlohmann::json to_json(const Taxonomy& taxonomy) {
  nlohmann::json categories = nlohmann::json::array();
  for (const auto& c : taxonomy.categories) {
    categories.push_back(c.name);
  }
  nlohmann::json branches = nlohmann::json::array();
  nlohmann::json branch_categories = nlohmann::json::object();
  for (const auto& b : taxonomy.branches) {
    branches.push_back(b.name);
    if (auto category = taxonomy.category_name_by_slug(b.category_slug)) {
      branch_categories[b.name] = std::move(*category);
    }
  }
  return {{"categories", std::move(categories)},
          {"branches", std::move(branches)},
          {"branch_categories", std::move(branch_categories)}};
}

}  // namespace sg::graph_service::domain
