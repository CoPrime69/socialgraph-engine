#include "sg/graph_service/app/graph_service.hpp"

#include <sg/common/errors.hpp>
#include <sg/common/log.hpp>

#include "sg/graph_service/domain/affinity.hpp"

namespace sg::graph_service::app {

GraphService::GraphService(GraphRepository& repository) : repository_(repository) {}

std::shared_ptr<const domain::Taxonomy> GraphService::taxonomy() {
  {
    std::lock_guard lock(taxonomy_mutex_);
    if (taxonomy_) {
      return taxonomy_;
    }
  }
  auto loaded = std::make_shared<const domain::Taxonomy>(repository_.load_taxonomy());
  if (loaded->empty()) {
    return loaded;  // not seeded yet: do not cache
  }
  std::lock_guard lock(taxonomy_mutex_);
  if (!taxonomy_) {
    taxonomy_ = std::move(loaded);
  }
  return taxonomy_;
}

domain::ProfileWithCount GraphService::profile(std::int64_t user_id) {
  auto found = repository_.find_profile(user_id);
  if (!found) {
    throw sg::common::not_found("User not found");
  }
  return std::move(*found);
}

domain::ProfileWithCount GraphService::my_profile(std::int64_t caller) {
  auto found = repository_.find_profile(caller);
  if (!found) {
    throw sg::common::not_found("Profile is still being provisioned, retry shortly");
  }
  return std::move(*found);
}

domain::UserPage GraphService::list_users(const domain::UserQuery& query) {
  if (query.category || query.branch) {
    const auto t = taxonomy();
    if (query.category && !t->has_category(*query.category)) {
      throw sg::common::bad_request("Unknown category '" + *query.category + "'");
    }
    if (query.branch && !t->has_branch(*query.branch)) {
      throw sg::common::bad_request("Unknown branch '" + *query.branch + "'");
    }
  }
  return repository_.list_profiles(query);
}

domain::ConnectionList GraphService::my_connections(std::int64_t caller) {
  if (!repository_.find_profile(caller)) {
    throw sg::common::not_found("Profile is still being provisioned, retry shortly");
  }
  return repository_.list_connections(caller);
}

domain::AddedConnection GraphService::connect(std::int64_t caller, std::int64_t other) {
  if (caller == other) {
    throw sg::common::validation_failed({{"user_id", "cannot connect to yourself"}});
  }
  const auto me = repository_.find_profile(caller);
  if (!me) {
    throw sg::common::not_found("Profile is still being provisioned, retry shortly");
  }
  const auto them = repository_.find_profile(other);
  if (!them) {
    throw sg::common::not_found("User not found");
  }
  // Profiles are immutable, so the strength computed here is the one stored atomically below.
  const double strength = domain::member_connection_strength(me->profile, them->profile, *taxonomy());
  auto added = repository_.add_connection(caller, other, strength);
  sg::common::log_info("connection added", {{"user_id", caller},
                                            {"other_id", other},
                                            {"strength", added.connection.strength},
                                            {"graph_version", added.graph_version}});
  return added;
}

std::int64_t GraphService::disconnect(std::int64_t caller, std::int64_t other) {
  const std::int64_t version = repository_.remove_connection(caller, other);
  sg::common::log_info("connection removed", {{"user_id", caller}, {"other_id", other}, {"graph_version", version}});
  return version;
}

domain::Snapshot GraphService::snapshot() { return repository_.snapshot(); }

void GraphService::record_dead_letter(const std::string& stream, const std::string& entry_id,
                                      const nlohmann::json& fields, const std::string& error) {
  repository_.record_dead_letter(stream, entry_id, fields, error);
}

EventOutcome GraphService::handle_identity_event(const sg::common::Event& event, const std::string& stream) {
  if (event.type != "user.registered") {
    return EventOutcome::ignored;
  }
  domain::NewMember member;
  try {
    member = domain::parse_user_registered(event.payload);
  } catch (const std::invalid_argument& e) {
    throw PoisonEvent(e.what());
  }
  const auto t = taxonomy();
  if (t->empty()) {
    // Not seeded yet: transient (retried, never dead-lettered).
    throw sg::common::unavailable("Taxonomy not loaded yet; cannot apply user.registered");
  }
  if (!t->has_category(member.category) || !t->has_branch(member.branch)) {
    throw PoisonEvent("user.registered: category or branch is not in the taxonomy");
  }
  if (!repository_.create_member_profile(member, stream, event.event_id)) {
    return EventOutcome::duplicate;
  }
  sg::common::log_info("member profile created", {{"user_id", member.user_id}, {"event_id", event.event_id}});
  return EventOutcome::applied;
}

}  // namespace sg::graph_service::app
