#include "sg/graph_service/domain/user_query.hpp"

#include <algorithm>
#include <cctype>

namespace sg::graph_service::domain {

std::optional<UserSort> parse_user_sort(std::string_view text) noexcept {
  if (text == "reputation") {
    return UserSort::reputation;
  }
  if (text == "name") {
    return UserSort::name;
  }
  if (text == "id") {
    return UserSort::id;
  }
  return std::nullopt;
}

bool contains_case_insensitive(std::string_view haystack, std::string_view needle) noexcept {
  const auto lower = [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };
  const auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
                              [&](char a, char b) { return lower(a) == lower(b); });
  return it != haystack.end() || needle.empty();
}

bool matches(const UserQuery& query, const UserProfile& profile) {
  if (query.q && !contains_case_insensitive(profile.handle, *query.q) &&
      !contains_case_insensitive(profile.name, *query.q)) {
    return false;
  }
  if (query.category && profile.category != *query.category) {
    return false;
  }
  if (query.branch && profile.branch != *query.branch) {
    return false;
  }
  return !query.min_reputation || profile.reputation >= *query.min_reputation;
}

nlohmann::json to_json(const UserPage& page) {
  nlohmann::json items = nlohmann::json::array();
  for (const auto& item : page.items) {
    items.push_back(to_json(item));
  }
  return {{"items", std::move(items)}, {"total", page.total}, {"limit", page.limit}, {"offset", page.offset}};
}

}  // namespace sg::graph_service::domain
