#pragma once

/// @file graph_repository.hpp
/// Persistence port of the graph application layer. Every mutating operation is atomic and keeps
/// graph_version gap-free: it bumps the version exactly once when (and only when) it changes the
/// graph, and stores the matching event in the outbox in the same transaction (contracts §7).

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "sg/graph_service/domain/events.hpp"
#include "sg/graph_service/domain/profile.hpp"
#include "sg/graph_service/domain/taxonomy.hpp"
#include "sg/graph_service/domain/user_query.hpp"

namespace sg::graph_service::app {

/// An event that can never be applied (malformed, or rejected by the schema). The consumer moves it
/// to the dead-letter table and acknowledges it instead of retrying forever.
class PoisonEvent : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

class GraphRepository {
 public:
  virtual ~GraphRepository() = default;

  /// Categories and branches in display order (empty before the dataset is loaded).
  [[nodiscard]] virtual domain::Taxonomy load_taxonomy() = 0;

  [[nodiscard]] virtual std::optional<domain::ProfileWithCount> find_profile(std::int64_t user_id) = 0;

  /// Filtered, sorted page of profiles; `query` is already validated.
  [[nodiscard]] virtual domain::UserPage list_profiles(const domain::UserQuery& query) = 0;

  /// Connections of `user_id` (strength desc, ties by the other user's id) and the graph version
  /// they reflect.
  [[nodiscard]] virtual domain::ConnectionList list_connections(std::int64_t user_id) = 0;

  /// Connects two existing profiles (origin "member") with `strength` and enqueues
  /// `connection.added`. @throws ApiError 404 when a profile does not exist, 409 when already connected.
  virtual domain::AddedConnection add_connection(std::int64_t user_id, std::int64_t other_id, double strength) = 0;

  /// Removes the connection and enqueues `connection.removed`; returns the new graph version.
  /// @throws ApiError 404 when the two users are not connected.
  virtual std::int64_t remove_connection(std::int64_t user_id, std::int64_t other_id) = 0;

  /// Applies a `user.registered` event at most once: false when (stream, event_id) was already
  /// processed or the profile exists. Otherwise inserts the member profile and enqueues
  /// `user.created`. @throws PoisonEvent when the schema rejects the profile.
  virtual bool create_member_profile(const domain::NewMember& member, const std::string& stream,
                                     const std::string& event_id) = 0;

  /// Keeps an event that can never be applied, for inspection.
  virtual void record_dead_letter(const std::string& stream, const std::string& entry_id,
                                  const nlohmann::json& fields, const std::string& error) = 0;

  /// A consistent copy of the whole graph and its version.
  [[nodiscard]] virtual domain::Snapshot snapshot() = 0;
};

}  // namespace sg::graph_service::app
