#pragma once

/// @file profile.hpp
/// Profiles and connections of the graph (contracts §5, §10.5).

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace sg::graph_service::domain {

/// Ids of self-registered members start here; imported Stack Overflow users are below (contracts §10.2).
inline constexpr std::int64_t kFirstMemberId = 100'000'000;

enum class ProfileSource { stackoverflow, member };
[[nodiscard]] std::string_view to_string(ProfileSource source) noexcept;
/// @throws std::invalid_argument for anything but "stackoverflow" / "member".
[[nodiscard]] ProfileSource parse_profile_source(std::string_view text);

struct UserProfile {
  std::int64_t id = 0;
  std::string handle;
  std::string name;
  std::string category;  ///< category name
  std::string branch;    ///< branch name
  double influence = 0.0;  ///< 0-100, 2 decimals
  std::int64_t reputation = 1;
  std::optional<std::string> location;
  std::optional<std::string> website_url;
  std::optional<std::string> about;
  std::int64_t views = 0;
  std::int64_t up_votes = 0;
  std::int64_t down_votes = 0;
  ProfileSource source = ProfileSource::member;
  std::int64_t created_at = 0;  ///< Unix seconds
  std::optional<std::int64_t> last_access_at;
};

/// `https://stackoverflow.com/users/<id>` for imported users, std::nullopt for members.
[[nodiscard]] std::optional<std::string> external_url(const UserProfile& profile);

/// The UserProfile JSON of contracts §10.5.
[[nodiscard]] nlohmann::json to_json(const UserProfile& profile);

/// A profile with its number of connections (GET /users, /users/{id}, /me/profile).
struct ProfileWithCount {
  UserProfile profile;
  std::int64_t connection_count = 0;
};
[[nodiscard]] nlohmann::json to_json(const ProfileWithCount& item);

enum class ConnectionOrigin { imported, member };
[[nodiscard]] std::string_view to_string(ConnectionOrigin origin) noexcept;
/// @throws std::invalid_argument for anything but "imported" / "member".
[[nodiscard]] ConnectionOrigin parse_connection_origin(std::string_view text);

/// One connection seen from one of its endpoints: `user` is the other endpoint.
struct ConnectionItem {
  UserProfile user;
  double strength = 0.0;  ///< 0-1, 3 decimals
  std::int64_t interactions = 0;
  ConnectionOrigin origin = ConnectionOrigin::member;
  std::int64_t connected_at = 0;  ///< Unix seconds
  std::optional<std::int64_t> first_interaction_at;
  std::optional<std::int64_t> last_interaction_at;
};
[[nodiscard]] nlohmann::json to_json(const ConnectionItem& item);

/// GET /me/connections: sorted by strength desc, ties by user id.
struct ConnectionList {
  std::vector<ConnectionItem> items;
  std::int64_t graph_version = 0;
};

/// Result of POST /me/connections.
struct AddedConnection {
  ConnectionItem connection;
  std::int64_t graph_version = 0;
};

/// An undirected edge, a < b.
struct Edge {
  std::int64_t a = 0;
  std::int64_t b = 0;
  double strength = 0.0;
};

/// GET /internal/v1/snapshot: a consistent view of the whole graph at graph_version.
struct Snapshot {
  std::int64_t graph_version = 0;
  std::vector<UserProfile> users;
  std::vector<Edge> edges;
};

/// Orders a pair as (min, max).
[[nodiscard]] inline std::pair<std::int64_t, std::int64_t> ordered(std::int64_t x, std::int64_t y) noexcept {
  return x < y ? std::pair{x, y} : std::pair{y, x};
}

}  // namespace sg::graph_service::domain
