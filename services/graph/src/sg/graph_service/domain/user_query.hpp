#pragma once

/// @file user_query.hpp
/// GET /api/v1/users parameters (contracts §5, §10.5).

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "sg/graph_service/domain/profile.hpp"

namespace sg::graph_service::domain {

/// `reputation` (desc, the default), `name` (case-insensitive asc) or `id` (asc); ties by id.
enum class UserSort { reputation, name, id };
[[nodiscard]] std::optional<UserSort> parse_user_sort(std::string_view text) noexcept;

inline constexpr std::size_t kSearchMaxLength = 100;  ///< characters of `q`
inline constexpr std::int64_t kDefaultPageSize = 20;
inline constexpr std::int64_t kMaxPageSize = 100;

struct UserQuery {
  std::optional<std::string> q;         ///< case-insensitive substring of handle or name
  std::optional<std::string> category;  ///< exact category name
  std::optional<std::string> branch;    ///< exact branch name
  std::optional<std::int64_t> min_reputation;
  UserSort sort = UserSort::reputation;
  std::int64_t limit = kDefaultPageSize;
  std::int64_t offset = 0;
};

/// True when `profile` matches the filters of `query` (reference semantics for repositories).
[[nodiscard]] bool matches(const UserQuery& query, const UserProfile& profile);

/// Case-insensitive (ASCII) substring test used by `q`.
[[nodiscard]] bool contains_case_insensitive(std::string_view haystack, std::string_view needle) noexcept;

struct UserPage {
  std::vector<ProfileWithCount> items;
  std::int64_t total = 0;
  std::int64_t limit = kDefaultPageSize;
  std::int64_t offset = 0;
};

/// `{ "items": [...], "total", "limit", "offset" }`.
[[nodiscard]] nlohmann::json to_json(const UserPage& page);

}  // namespace sg::graph_service::domain
