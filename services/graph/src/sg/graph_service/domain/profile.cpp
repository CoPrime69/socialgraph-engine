#include "sg/graph_service/domain/profile.hpp"

#include <stdexcept>
#include <string>

#include <sg/common/time.hpp>
#include <sg/common/validation.hpp>

namespace sg::graph_service::domain {
namespace {

nlohmann::json optional_text(const std::optional<std::string>& value) {
  return value ? nlohmann::json(*value) : nlohmann::json(nullptr);
}

nlohmann::json optional_time(const std::optional<std::int64_t>& value) {
  return value ? nlohmann::json(sg::common::unix_to_rfc3339(*value)) : nlohmann::json(nullptr);
}

}  // namespace

std::string_view to_string(ProfileSource source) noexcept {
  return source == ProfileSource::stackoverflow ? "stackoverflow" : "member";
}

ProfileSource parse_profile_source(std::string_view text) {
  if (text == "stackoverflow") {
    return ProfileSource::stackoverflow;
  }
  if (text == "member") {
    return ProfileSource::member;
  }
  throw std::invalid_argument("unknown profile source '" + std::string(text) + "'");
}

std::string_view to_string(ConnectionOrigin origin) noexcept {
  return origin == ConnectionOrigin::imported ? "imported" : "member";
}

ConnectionOrigin parse_connection_origin(std::string_view text) {
  if (text == "imported") {
    return ConnectionOrigin::imported;
  }
  if (text == "member") {
    return ConnectionOrigin::member;
  }
  throw std::invalid_argument("unknown connection origin '" + std::string(text) + "'");
}

std::optional<std::string> external_url(const UserProfile& profile) {
  if (profile.source != ProfileSource::stackoverflow) {
    return std::nullopt;
  }
  return "https://stackoverflow.com/users/" + std::to_string(profile.id);
}

nlohmann::json to_json(const UserProfile& p) {
  return {
      {"id", p.id},
      {"handle", p.handle},
      {"name", p.name},
      {"category", p.category},
      {"branch", p.branch},
      {"influence", sg::common::round_influence(p.influence)},
      {"reputation", p.reputation},
      {"location", optional_text(p.location)},
      {"website_url", optional_text(p.website_url)},
      {"about", optional_text(p.about)},
      {"views", p.views},
      {"up_votes", p.up_votes},
      {"down_votes", p.down_votes},
      {"source", std::string(to_string(p.source))},
      {"external_url", optional_text(external_url(p))},
      {"created_at", sg::common::unix_to_rfc3339(p.created_at)},
      {"last_access_at", optional_time(p.last_access_at)},
  };
}

nlohmann::json to_json(const ProfileWithCount& item) {
  nlohmann::json out = to_json(item.profile);
  out["connection_count"] = item.connection_count;
  return out;
}

nlohmann::json to_json(const ConnectionItem& c) {
  return {
      {"user", to_json(c.user)},
      {"strength", sg::common::round3(c.strength)},
      {"interactions", c.interactions},
      {"origin", std::string(to_string(c.origin))},
      {"connected_at", sg::common::unix_to_rfc3339(c.connected_at)},
      {"first_interaction_at", optional_time(c.first_interaction_at)},
      {"last_interaction_at", optional_time(c.last_interaction_at)},
  };
}

}  // namespace sg::graph_service::domain
