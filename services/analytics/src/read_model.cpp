#include "read_model.hpp"

#include <algorithm>
#include <stdexcept>

namespace sg::analytics {

namespace {

std::uint64_t required_version(const nlohmann::json& payload) {
  const auto& v = payload.at("graph_version");
  if (!v.is_number_unsigned() && !(v.is_number_integer() && v.get<std::int64_t>() >= 0)) {
    throw std::invalid_argument("graph_version must be a non-negative integer");
  }
  return v.get<std::uint64_t>();
}

std::pair<std::int64_t, std::int64_t> ordered_pair(const nlohmann::json& payload) {
  auto a = payload.at("user_a").get<std::int64_t>();
  auto b = payload.at("user_b").get<std::int64_t>();
  if (a == b) throw std::invalid_argument("self connection in event");
  return a < b ? std::pair{a, b} : std::pair{b, a};
}

}  // namespace

UserInfo parse_user(const nlohmann::json& user) {
  UserInfo info;
  info.id = user.at("id").get<std::int64_t>();
  info.handle = user.at("handle").get<std::string>();
  info.name = user.at("name").get<std::string>();
  info.category = user.at("category").get<std::string>();
  info.branch = user.at("branch").get<std::string>();
  const auto& influence = user.at("influence");
  info.influence = influence.is_string() ? std::stod(influence.get<std::string>()) : influence.get<double>();
  if (auto it = user.find("reputation"); it != user.end() && it->is_number()) info.reputation = it->get<std::int64_t>();
  return info;
}

Snapshot parse_snapshot(const nlohmann::json& body) {
  Snapshot snapshot;
  snapshot.version = body.at("graph_version").get<std::uint64_t>();
  for (const auto& user : body.at("users")) snapshot.users.push_back(parse_user(user));
  for (const auto& edge : body.at("edges")) {
    if (!edge.is_array() || edge.size() != 3) throw std::invalid_argument("edge must be [a, b, strength]");
    auto a = edge[0].get<std::int64_t>();
    auto b = edge[1].get<std::int64_t>();
    if (a > b) std::swap(a, b);
    snapshot.edges.push_back({a, b, edge[2].get<double>()});
  }
  return snapshot;
}

void ReadModel::reset(std::uint64_t version, std::vector<UserInfo> users, std::vector<EdgeInfo> edges) {
  {
    std::unique_lock lock(mutex_);
    users_.clear();
    edges_.clear();
    users_.reserve(users.size());
    for (auto& user : users) users_[user.id] = std::move(user);
    edges_.reserve(edges.size());
    for (const auto& edge : edges) {
      if (edge.user_a == edge.user_b || !users_.contains(edge.user_a) || !users_.contains(edge.user_b)) {
        throw std::invalid_argument("snapshot edge references an unknown user or is a self loop");
      }
      edges_[EdgeKey{edge.user_a, edge.user_b}] = edge;
    }
    version_ = version;
    ready_ = true;
  }
  publish_version(version);
}

ApplyResult ReadModel::apply(const sg::common::Event& event) {
  const bool known = event.type == "user.created" || event.type == "connection.added" ||
                     event.type == "connection.removed";
  if (!known) return ApplyResult::ignored;

  const std::uint64_t event_version = required_version(event.payload);
  std::uint64_t new_version = 0;
  {
    std::unique_lock lock(mutex_);
    if (event_version <= version_) return ApplyResult::duplicate;
    if (event_version != version_ + 1) return ApplyResult::gap;

    if (event.type == "user.created") {
      UserInfo user = parse_user(event.payload.at("user"));
      users_[user.id] = std::move(user);
    } else {
      auto [a, b] = ordered_pair(event.payload);
      if (event.type == "connection.added") {
        // A connection to a user we never saw means our state diverged; force a resync.
        if (!users_.contains(a) || !users_.contains(b)) return ApplyResult::gap;
        edges_[EdgeKey{a, b}] = EdgeInfo{a, b, event.payload.at("strength").get<double>()};
      } else {
        edges_.erase(EdgeKey{a, b});
      }
    }
    version_ = event_version;
    new_version = version_;
  }
  publish_version(new_version);
  return ApplyResult::applied;
}

bool ReadModel::ready() const {
  std::shared_lock lock(mutex_);
  return ready_;
}

std::uint64_t ReadModel::version() const {
  std::shared_lock lock(mutex_);
  return version_;
}

void ReadModel::publish_version(std::uint64_t version) {
  {
    std::lock_guard lock(version_mutex_);
    published_version_ = version;
  }
  version_cv_.notify_all();
}

bool ReadModel::wait_for_version(std::uint64_t target, std::chrono::milliseconds timeout) const {
  std::unique_lock lock(version_mutex_);
  return version_cv_.wait_for(lock, timeout, [&] { return published_version_ >= target; });
}

std::shared_ptr<const GraphState> ReadModel::materialize() const {
  auto state = std::make_shared<GraphState>();
  {
    std::shared_lock lock(mutex_);
    state->version = version_;
    state->users.reserve(users_.size());
    for (const auto& [id, user] : users_) state->users.push_back(user);
    state->edges.reserve(edges_.size());
    for (const auto& [k, edge] : edges_) state->edges.push_back(edge);
  }
  std::ranges::sort(state->users, {}, &UserInfo::id);
  std::ranges::sort(state->edges, [](const EdgeInfo& l, const EdgeInfo& r) {
    return std::tie(l.user_a, l.user_b) < std::tie(r.user_a, r.user_b);
  });
  return state;
}

}  // namespace sg::analytics
