#pragma once

#include "read_model.hpp"

#include <sg/common/events.hpp>

#include <string>
#include <vector>

namespace sg::analytics::testing {

inline UserInfo make_user(std::int64_t id, std::string category = "Engineering", std::string branch = "software",
                          double influence = 50.0) {
  return UserInfo{id, "user" + std::to_string(id), "User " + std::to_string(id), std::move(category),
                  std::move(branch), influence};
}

inline nlohmann::json user_json(const UserInfo& u) {
  return {{"id", u.id},         {"handle", u.handle}, {"name", u.name},
          {"category", u.category}, {"branch", u.branch}, {"influence", u.influence},
          {"created_at", "2026-10-19T00:00:00Z"}};
}

inline sg::common::Event connection_added(std::uint64_t version, std::int64_t a, std::int64_t b,
                                          double strength = 0.5) {
  return {std::to_string(version), "connection.added", 1, "2026-10-19T00:00:00Z",
          {{"graph_version", version}, {"user_a", a}, {"user_b", b}, {"strength", strength}}};
}

inline sg::common::Event connection_removed(std::uint64_t version, std::int64_t a, std::int64_t b) {
  return {std::to_string(version), "connection.removed", 1, "2026-10-19T00:00:00Z",
          {{"graph_version", version}, {"user_a", a}, {"user_b", b}}};
}

inline sg::common::Event user_created(std::uint64_t version, const UserInfo& user) {
  return {std::to_string(version), "user.created", 1, "2026-10-19T00:00:00Z",
          {{"graph_version", version}, {"user", user_json(user)}}};
}

// Two 4-cliques {1..4} (Engineering/software) and {5..8} (Finance/accounting) joined by the bridge 4-5,
// plus an isolated user 9. Version 10.
inline void load_two_cliques(ReadModel& model) {
  std::vector<UserInfo> users;
  for (int i = 1; i <= 4; ++i) users.push_back(make_user(i, "Engineering", "software", 60.0 + i));
  for (int i = 5; i <= 8; ++i) users.push_back(make_user(i, "Finance", "accounting", 40.0 + i));
  users.push_back(make_user(9, "Sales", "sales", 10.0));
  std::vector<EdgeInfo> edges;
  for (int base : {1, 5}) {
    for (int a = base; a < base + 4; ++a) {
      for (int b = a + 1; b < base + 4; ++b) edges.push_back({a, b, 0.9});
    }
  }
  edges.push_back({4, 5, 0.3});
  model.reset(10, std::move(users), std::move(edges));
}

}  // namespace sg::analytics::testing
