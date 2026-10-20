#pragma once

// In-memory GraphRepository with the same observable semantics as the PostgreSQL one: one mutex plays
// the role of the graph_meta row lock, the version is bumped once per change and events are recorded
// in the same critical section (the "outbox").

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <sg/common/errors.hpp>
#include <sg/common/time.hpp>
#include <sg/common/validation.hpp>

#include "sg/graph_service/app/graph_repository.hpp"

namespace sg::graph_service::testing {

struct RecordedEvent {
  std::string type;
  nlohmann::json payload;
};

struct StoredConnection {
  double strength = 0.0;
  std::int64_t interactions = 0;
  domain::ConnectionOrigin origin = domain::ConnectionOrigin::member;
  std::int64_t created_at = 0;
  std::optional<std::int64_t> first_interaction_at;
  std::optional<std::int64_t> last_interaction_at;
};

inline domain::Taxonomy test_taxonomy() {
  domain::Taxonomy t;
  t.categories = {{"web", "Web Development", "Web"}, {"data", "Databases & Data", "Data"}, {"systems", "Systems Programming", "Native"}};
  t.branches = {{"javascript", "web"}, {"php", "web"}, {"sql", "data"}, {"c++", "systems"}};
  return t;
}

inline domain::UserProfile imported_profile(std::int64_t id, const std::string& handle, const std::string& name,
                                            const std::string& category, const std::string& branch, double influence,
                                            std::int64_t reputation) {
  domain::UserProfile p;
  p.id = id;
  p.handle = handle;
  p.name = name;
  p.category = category;
  p.branch = branch;
  p.influence = influence;
  p.reputation = reputation;
  p.source = domain::ProfileSource::stackoverflow;
  p.created_at = 1217548800;  // 2008-08-01
  p.last_access_at = 1704067200;
  return p;
}

class InMemoryGraphRepository final : public app::GraphRepository {
 public:
  domain::Taxonomy load_taxonomy() override {
    std::lock_guard lock(mutex_);
    ++taxonomy_loads_;
    return taxonomy_;
  }

  std::optional<domain::ProfileWithCount> find_profile(std::int64_t user_id) override {
    std::lock_guard lock(mutex_);
    const auto it = profiles_.find(user_id);
    if (it == profiles_.end()) {
      return std::nullopt;
    }
    return domain::ProfileWithCount{it->second, degree(user_id)};
  }

  domain::UserPage list_profiles(const domain::UserQuery& query) override {
    std::lock_guard lock(mutex_);
    std::vector<domain::ProfileWithCount> all;
    for (const auto& [id, p] : profiles_) {
      if (domain::matches(query, p)) {
        all.push_back({p, degree(id)});
      }
    }
    std::sort(all.begin(), all.end(), [&](const auto& x, const auto& y) {
      switch (query.sort) {
        case domain::UserSort::name: {
          auto lx = lower(x.profile.name);
          auto ly = lower(y.profile.name);
          return lx != ly ? lx < ly : x.profile.id < y.profile.id;
        }
        case domain::UserSort::id:
          return x.profile.id < y.profile.id;
        case domain::UserSort::reputation:
          break;
      }
      return x.profile.reputation != y.profile.reputation ? x.profile.reputation > y.profile.reputation
                                                          : x.profile.id < y.profile.id;
    });
    domain::UserPage page;
    page.total = static_cast<std::int64_t>(all.size());
    page.limit = query.limit;
    page.offset = query.offset;
    for (std::int64_t i = query.offset; i < page.total && i < query.offset + query.limit; ++i) {
      page.items.push_back(all[static_cast<std::size_t>(i)]);
    }
    return page;
  }

  domain::ConnectionList list_connections(std::int64_t user_id) override {
    std::lock_guard lock(mutex_);
    domain::ConnectionList list;
    list.graph_version = version_;
    for (const auto& [key, c] : connections_) {
      if (key.first != user_id && key.second != user_id) {
        continue;
      }
      const std::int64_t other = key.first == user_id ? key.second : key.first;
      domain::ConnectionItem item;
      item.user = profiles_.at(other);
      item.strength = c.strength;
      item.interactions = c.interactions;
      item.origin = c.origin;
      item.connected_at = c.created_at;
      item.first_interaction_at = c.first_interaction_at;
      item.last_interaction_at = c.last_interaction_at;
      list.items.push_back(std::move(item));
    }
    std::sort(list.items.begin(), list.items.end(), [](const auto& x, const auto& y) {
      return x.strength != y.strength ? x.strength > y.strength : x.user.id < y.user.id;
    });
    return list;
  }

  domain::AddedConnection add_connection(std::int64_t user_id, std::int64_t other_id, double strength) override {
    std::lock_guard lock(mutex_);
    if (!profiles_.contains(user_id)) {
      throw sg::common::not_found("Profile is still being provisioned, retry shortly");
    }
    if (!profiles_.contains(other_id)) {
      throw sg::common::not_found("User not found");
    }
    const auto key = domain::ordered(user_id, other_id);
    if (connections_.contains(key)) {
      throw sg::common::conflict("Already connected");
    }
    StoredConnection c;
    c.strength = sg::common::round3(strength);
    c.created_at = sg::common::unix_now();
    connections_[key] = c;
    ++version_;
    events_.push_back({"connection.added", domain::connection_added_payload(version_, key.first, key.second, c.strength)});
    domain::AddedConnection added;
    added.graph_version = version_;
    added.connection.user = profiles_.at(other_id);
    added.connection.strength = c.strength;
    added.connection.connected_at = c.created_at;
    return added;
  }

  std::int64_t remove_connection(std::int64_t user_id, std::int64_t other_id) override {
    std::lock_guard lock(mutex_);
    const auto key = domain::ordered(user_id, other_id);
    if (connections_.erase(key) == 0) {
      throw sg::common::not_found("Not connected to this user");
    }
    ++version_;
    events_.push_back({"connection.removed", domain::connection_removed_payload(version_, key.first, key.second)});
    return version_;
  }

  bool create_member_profile(const domain::NewMember& member, const std::string& stream,
                             const std::string& event_id) override {
    std::lock_guard lock(mutex_);
    if (!processed_.insert({stream, event_id}).second || profiles_.contains(member.user_id)) {
      return false;
    }
    if (!taxonomy_.has_category(member.category) || !taxonomy_.has_branch(member.branch)) {
      processed_.erase({stream, event_id});
      throw app::PoisonEvent("foreign key violation");
    }
    const auto profile = domain::member_profile(member, sg::common::unix_now());
    profiles_[member.user_id] = profile;
    ++version_;
    events_.push_back({"user.created", domain::user_created_payload(version_, profile)});
    return true;
  }

  void record_dead_letter(const std::string& stream, const std::string& entry_id, const nlohmann::json& fields,
                          const std::string& error) override {
    std::lock_guard lock(mutex_);
    dead_letters_.push_back({stream + "/" + entry_id, {{"fields", fields}, {"error", error}}});
  }

  domain::Snapshot snapshot() override {
    std::lock_guard lock(mutex_);
    domain::Snapshot s;
    s.graph_version = version_;
    for (const auto& [id, p] : profiles_) {
      s.users.push_back(p);
    }
    for (const auto& [key, c] : connections_) {
      s.edges.push_back({key.first, key.second, c.strength});
    }
    return s;
  }

  // Test helpers.
  void set_taxonomy(domain::Taxonomy t) {
    std::lock_guard lock(mutex_);
    taxonomy_ = std::move(t);
  }
  void put_profile(const domain::UserProfile& p) {
    std::lock_guard lock(mutex_);
    profiles_[p.id] = p;
  }
  void put_connection(std::int64_t a, std::int64_t b, double strength) {
    std::lock_guard lock(mutex_);
    StoredConnection c;
    c.strength = strength;
    c.interactions = 2;
    c.origin = domain::ConnectionOrigin::imported;
    c.created_at = 1217548800;
    c.first_interaction_at = 1217548800;
    c.last_interaction_at = 1220227200;
    connections_[domain::ordered(a, b)] = c;
  }
  void set_version(std::int64_t v) {
    std::lock_guard lock(mutex_);
    version_ = v;
  }
  [[nodiscard]] std::int64_t version() const {
    std::lock_guard lock(mutex_);
    return version_;
  }
  [[nodiscard]] std::vector<RecordedEvent> events() const {
    std::lock_guard lock(mutex_);
    return events_;
  }
  [[nodiscard]] std::vector<RecordedEvent> dead_letters() const {
    std::lock_guard lock(mutex_);
    return dead_letters_;
  }
  [[nodiscard]] int taxonomy_loads() const {
    std::lock_guard lock(mutex_);
    return taxonomy_loads_;
  }

 private:
  static std::string lower(std::string s) {
    for (auto& c : s) {
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
  }
  std::int64_t degree(std::int64_t id) const {
    std::int64_t n = 0;
    for (const auto& [key, c] : connections_) {
      n += (key.first == id || key.second == id) ? 1 : 0;
    }
    return n;
  }

  mutable std::mutex mutex_;
  domain::Taxonomy taxonomy_ = test_taxonomy();
  std::map<std::int64_t, domain::UserProfile> profiles_;
  std::map<std::pair<std::int64_t, std::int64_t>, StoredConnection> connections_;
  std::set<std::pair<std::string, std::string>> processed_;
  std::vector<RecordedEvent> events_;
  std::vector<RecordedEvent> dead_letters_;
  std::int64_t version_ = 1;
  int taxonomy_loads_ = 0;
};

/// A small graph: three imported users and the member 100000001.
inline void populate(InMemoryGraphRepository& repo) {
  repo.put_profile(imported_profile(1, "jeff_atwood_1", "Jeff Atwood", "Web Development", "javascript", 97.03, 63708));
  repo.put_profile(imported_profile(2, "geoff_2", "Geoff Dalgas", "Databases & Data", "sql", 72.42, 6156));
  repo.put_profile(imported_profile(3, "jarrod_3", "Jarrod Dixon", "Databases & Data", "sql", 86.36, 15767));
  domain::NewMember m;
  m.user_id = 100000001;
  m.handle = "ada_l";
  m.name = "Ada Lovelace";
  m.category = "Web Development";
  m.branch = "javascript";
  repo.put_profile(domain::member_profile(m, 1760900000));
  repo.put_connection(1, 2, 0.255);
  repo.put_connection(2, 3, 0.661);
}

}  // namespace sg::graph_service::testing
