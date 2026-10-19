#pragma once

/// @file graph_service.hpp
/// Graph use cases (contracts §5, §10.5): profiles, search, connections, snapshot and the
/// identity.events handler.

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include <sg/common/events.hpp>

#include "sg/graph_service/app/graph_repository.hpp"

namespace sg::graph_service::app {

enum class EventOutcome {
  applied,    ///< the graph changed
  duplicate,  ///< already applied earlier (redelivery)
  ignored,    ///< an event type graph-service does not consume
};

class GraphService {
 public:
  /// `repository` must outlive the service.
  explicit GraphService(GraphRepository& repository);

  /// The taxonomy (cached once loaded; it only changes when an empty database is seeded).
  [[nodiscard]] std::shared_ptr<const domain::Taxonomy> taxonomy();

  /// @throws ApiError 404 when the user does not exist.
  [[nodiscard]] domain::ProfileWithCount profile(std::int64_t user_id);

  /// The caller's own profile. @throws ApiError 404 while it is still being provisioned.
  [[nodiscard]] domain::ProfileWithCount my_profile(std::int64_t caller);

  /// @throws ApiError 400 when the category or branch filter is not in the taxonomy.
  [[nodiscard]] domain::UserPage list_users(const domain::UserQuery& query);

  /// @throws ApiError 404 while the caller's profile is still being provisioned.
  [[nodiscard]] domain::ConnectionList my_connections(std::int64_t caller);

  /// Connects the caller to `other` with strength = affinity (contracts §8).
  /// @throws ApiError 422 for the caller itself, 404 for an unknown user (or the caller's profile
  ///         not provisioned yet), 409 when already connected.
  domain::AddedConnection connect(std::int64_t caller, std::int64_t other);

  /// @throws ApiError 404 when not connected. Returns the new graph version.
  std::int64_t disconnect(std::int64_t caller, std::int64_t other);

  [[nodiscard]] domain::Snapshot snapshot();

  /// Keeps an event that can never be applied (see PoisonEvent).
  void record_dead_letter(const std::string& stream, const std::string& entry_id, const nlohmann::json& fields,
                          const std::string& error);

  /// Handles one identity.events event idempotently.
  /// @throws PoisonEvent when it can never be applied; anything else is transient (retry).
  EventOutcome handle_identity_event(const sg::common::Event& event, const std::string& stream);

 private:
  GraphRepository& repository_;
  std::mutex taxonomy_mutex_;
  std::shared_ptr<const domain::Taxonomy> taxonomy_;
};

}  // namespace sg::graph_service::app
