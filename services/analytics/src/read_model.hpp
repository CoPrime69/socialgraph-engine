#pragma once

#include <sg/common/events.hpp>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace sg::analytics {

struct UserInfo {
  std::int64_t id = 0;
  std::string handle;
  std::string name;
  std::string category;
  std::string branch;
  double influence = 0.0;
  std::int64_t reputation = 0;
};

struct EdgeInfo {
  std::int64_t user_a = 0;  // always user_a < user_b
  std::int64_t user_b = 0;
  double strength = 0.0;
};

// Immutable copy of the graph at one version. Users are ordered by id so that dense node
// indices (position in `users`) are stable for a given set of users.
struct GraphState {
  std::uint64_t version = 0;
  std::vector<UserInfo> users;
  std::vector<EdgeInfo> edges;  // sorted by (user_a, user_b)
};

enum class ApplyResult { applied, duplicate, gap, ignored };

// The in-memory read model of the social graph, kept current from graph.events.
//
// Writers (the single event-consumer thread) mutate it under an exclusive lock; HTTP readers only
// call materialize()/version(), so the lock is held for O(V+E) at most. Versions follow
// contracts §7: events must be applied strictly in graph_version order.
class ReadModel {
 public:
  // Replaces the whole model, e.g. from GET /internal/v1/snapshot.
  void reset(std::uint64_t version, std::vector<UserInfo> users, std::vector<EdgeInfo> edges);

  // Applies one graph.events entry. Unknown event types are ignored (forward compatibility).
  // Throws std::invalid_argument for a malformed payload of a known type.
  ApplyResult apply(const sg::common::Event& event);

  [[nodiscard]] bool ready() const;
  [[nodiscard]] std::uint64_t version() const;

  // Blocks until version() >= target or the timeout expires; returns whether the target was reached.
  bool wait_for_version(std::uint64_t target, std::chrono::milliseconds timeout) const;

  // Consistent copy of the current graph.
  [[nodiscard]] std::shared_ptr<const GraphState> materialize() const;

 private:
  using EdgeKey = std::pair<std::int64_t, std::int64_t>;
  struct EdgeKeyHash {
    std::size_t operator()(const EdgeKey& k) const noexcept {
      return std::hash<std::int64_t>{}(k.first) * 0x9E3779B97F4A7C15ULL ^ std::hash<std::int64_t>{}(k.second);
    }
  };
  void publish_version(std::uint64_t version);

  mutable std::shared_mutex mutex_;
  bool ready_ = false;
  std::uint64_t version_ = 0;
  std::unordered_map<std::int64_t, UserInfo> users_;
  std::unordered_map<EdgeKey, EdgeInfo, EdgeKeyHash> edges_;

  mutable std::mutex version_mutex_;
  mutable std::condition_variable version_cv_;
  std::uint64_t published_version_ = 0;
};

// Parses the JSON body of GET /internal/v1/snapshot into (version, users, edges).
struct Snapshot {
  std::uint64_t version = 0;
  std::vector<UserInfo> users;
  std::vector<EdgeInfo> edges;
};
Snapshot parse_snapshot(const nlohmann::json& body);
UserInfo parse_user(const nlohmann::json& user);

}  // namespace sg::analytics
