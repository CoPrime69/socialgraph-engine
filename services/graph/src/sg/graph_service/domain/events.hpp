#pragma once

/// @file events.hpp
/// Event payloads consumed and produced by graph-service (contracts §7, §10.4, §10.5).

#include <cstdint>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "sg/graph_service/domain/profile.hpp"

namespace sg::graph_service::domain {

/// A member announced by identity-service in `user.registered`.
struct NewMember {
  std::int64_t user_id = 0;
  std::string handle;
  std::string name;
  std::string category;
  std::string branch;
  std::optional<std::string> location;
  std::optional<std::string> about;
};

/// Parses and validates a `user.registered` payload
/// `{ user_id, handle, name, category, branch, location, about }` with the registration rules
/// (user_id >= 100,000,000; handle/name rules; location <= 100 and about <= 500 characters).
/// @throws std::invalid_argument when the payload can never be applied.
[[nodiscard]] NewMember parse_user_registered(const nlohmann::json& payload);

/// The profile of a new member: reputation 1, influence 0, no Stack Overflow statistics.
[[nodiscard]] UserProfile member_profile(const NewMember& member, std::int64_t created_at);

/// `user.created`: `{ graph_version, user: UserProfile }`.
[[nodiscard]] nlohmann::json user_created_payload(std::int64_t graph_version, const UserProfile& profile);
/// `connection.added`: `{ graph_version, user_a, user_b, strength }` with user_a < user_b.
[[nodiscard]] nlohmann::json connection_added_payload(std::int64_t graph_version, std::int64_t user_a,
                                                      std::int64_t user_b, double strength);
/// `connection.removed`: `{ graph_version, user_a, user_b }` with user_a < user_b.
[[nodiscard]] nlohmann::json connection_removed_payload(std::int64_t graph_version, std::int64_t user_a,
                                                        std::int64_t user_b);

/// GET /internal/v1/snapshot: `{ graph_version, users: [UserProfile], edges: [[a, b, strength]] }`.
[[nodiscard]] nlohmann::json to_json(const Snapshot& snapshot);

}  // namespace sg::graph_service::domain
