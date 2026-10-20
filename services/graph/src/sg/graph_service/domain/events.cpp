#include "sg/graph_service/domain/events.hpp"

#include <stdexcept>
#include <utility>

#include <sg/common/validation.hpp>

namespace sg::graph_service::domain {
namespace {

constexpr std::size_t kTaxonomyNameMaxLength = 64;
constexpr std::size_t kLocationMaxLength = 100;
constexpr std::size_t kAboutMaxLength = 500;

const nlohmann::json& field(const nlohmann::json& payload, const char* name) {
  const auto it = payload.find(name);
  if (it == payload.end()) {
    throw std::invalid_argument(std::string("user.registered: missing '") + name + "'");
  }
  return *it;
}

std::string string_field(const nlohmann::json& payload, const char* name) {
  const auto& value = field(payload, name);
  if (!value.is_string()) {
    throw std::invalid_argument(std::string("user.registered: '") + name + "' must be a string");
  }
  return value.get<std::string>();
}

std::optional<std::string> optional_text(const nlohmann::json& payload, const char* name, std::size_t max,
                                         bool allow_newlines) {
  const auto it = payload.find(name);
  if (it == payload.end() || it->is_null()) {
    return std::nullopt;
  }
  if (!it->is_string()) {
    throw std::invalid_argument(std::string("user.registered: '") + name + "' must be a string or null");
  }
  const auto& raw = it->get_ref<const std::string&>();
  if (raw.find_first_not_of(" \t\r\n") == std::string::npos) {
    return std::nullopt;
  }
  auto text = sg::common::normalize_text(raw, max, allow_newlines);
  if (!text) {
    throw std::invalid_argument(std::string("user.registered: '") + name + "' is invalid");
  }
  return text;
}

}  // namespace

NewMember parse_user_registered(const nlohmann::json& payload) {
  if (!payload.is_object()) {
    throw std::invalid_argument("user.registered: payload must be an object");
  }
  NewMember m;
  const auto& id = field(payload, "user_id");
  if (!id.is_number_integer()) {
    throw std::invalid_argument("user.registered: 'user_id' must be an integer");
  }
  m.user_id = id.get<std::int64_t>();
  if (m.user_id < kFirstMemberId) {
    throw std::invalid_argument("user.registered: 'user_id' must be >= 100000000");
  }
  m.handle = string_field(payload, "handle");
  if (!sg::common::is_valid_handle(m.handle)) {
    throw std::invalid_argument("user.registered: 'handle' is invalid");
  }
  auto name = sg::common::normalize_name(string_field(payload, "name"));
  if (!name) {
    throw std::invalid_argument("user.registered: 'name' is invalid");
  }
  m.name = std::move(*name);
  for (auto [key, target] : {std::pair{"category", &m.category}, std::pair{"branch", &m.branch}}) {
    auto value = sg::common::normalize_text(string_field(payload, key), kTaxonomyNameMaxLength);
    if (!value) {
      throw std::invalid_argument(std::string("user.registered: '") + key + "' is invalid");
    }
    *target = std::move(*value);
  }
  m.location = optional_text(payload, "location", kLocationMaxLength, false);
  m.about = optional_text(payload, "about", kAboutMaxLength, true);
  return m;
}

UserProfile member_profile(const NewMember& member, std::int64_t created_at) {
  UserProfile p;
  p.id = member.user_id;
  p.handle = member.handle;
  p.name = member.name;
  p.category = member.category;
  p.branch = member.branch;
  p.influence = 0.0;
  p.reputation = 1;
  p.location = member.location;
  p.about = member.about;
  p.source = ProfileSource::member;
  p.created_at = created_at;
  return p;
}

nlohmann::json user_created_payload(std::int64_t graph_version, const UserProfile& profile) {
  return {{"graph_version", graph_version}, {"user", to_json(profile)}};
}

nlohmann::json connection_added_payload(std::int64_t graph_version, std::int64_t user_a, std::int64_t user_b,
                                        double strength) {
  const auto [a, b] = ordered(user_a, user_b);
  return {{"graph_version", graph_version}, {"user_a", a}, {"user_b", b}, {"strength", sg::common::round3(strength)}};
}

nlohmann::json connection_removed_payload(std::int64_t graph_version, std::int64_t user_a, std::int64_t user_b) {
  const auto [a, b] = ordered(user_a, user_b);
  return {{"graph_version", graph_version}, {"user_a", a}, {"user_b", b}};
}

nlohmann::json to_json(const Snapshot& snapshot) {
  nlohmann::json users = nlohmann::json::array();
  users.get_ref<nlohmann::json::array_t&>().reserve(snapshot.users.size());
  for (const auto& user : snapshot.users) {
    users.push_back(to_json(user));
  }
  nlohmann::json edges = nlohmann::json::array();
  edges.get_ref<nlohmann::json::array_t&>().reserve(snapshot.edges.size());
  for (const auto& e : snapshot.edges) {
    edges.push_back(nlohmann::json::array({e.a, e.b, sg::common::round3(e.strength)}));
  }
  return {{"graph_version", snapshot.graph_version}, {"users", std::move(users)}, {"edges", std::move(edges)}};
}

}  // namespace sg::graph_service::domain
