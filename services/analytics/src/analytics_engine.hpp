#pragma once

#include "read_model.hpp"

#include <sg/graph/graphcore.hpp>

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace sg::analytics {

enum class Metric : std::size_t { pagerank = 0, betweenness = 1, closeness = 2, degree = 3 };
std::optional<Metric> parse_metric(std::string_view name);
std::string_view metric_name(Metric metric);

struct EngineOptions {
  unsigned threads = 0;                             // 0 = hardware concurrency
  std::size_t exact_betweenness_max_nodes = 5000;   // above this, Brandes runs on sampled pivots
  std::size_t betweenness_samples = 256;
};

// Everything derived from one GraphState. Immutable once built, shared between request threads.
struct Analytics {
  std::shared_ptr<const GraphState> state;
  std::unordered_map<std::int64_t, sg::graph::NodeId> node_of;  // user id -> dense node index
  std::vector<sg::graph::Profile> profiles;
  sg::graph::CsrGraph graph;  // weights = louvain_weight(strength)
  std::array<std::vector<double>, 4> scores;          // indexed by Metric
  std::array<std::vector<std::uint32_t>, 4> ranks;    // 1-based rank per node, indexed by Metric
  std::array<std::vector<sg::graph::NodeId>, 4> order; // nodes sorted by rank, indexed by Metric
  bool betweenness_approximate = false;
  sg::graph::CommunityResult communities;
  sg::graph::ComponentsResult components;
  std::string computed_at;
  double compute_ms = 0.0;

  [[nodiscard]] std::uint64_t version() const { return state->version; }
  [[nodiscard]] std::optional<sg::graph::NodeId> node(std::int64_t user_id) const;
  [[nodiscard]] const UserInfo& user(sg::graph::NodeId node) const { return state->users[node]; }
  [[nodiscard]] const std::vector<double>& metric(Metric m) const { return scores[static_cast<std::size_t>(m)]; }
};

// Computes all analytics for a graph state. Pure function: no locks, no I/O. Complexity is dominated
// by Brandes betweenness, O(V·E) exact or O(k·E) sampled.
std::shared_ptr<const Analytics> compute_analytics(std::shared_ptr<const GraphState> state,
                                                   const EngineOptions& options);

// Caches the analytics of the latest read-model version. The first request after a change computes
// (single-flight: concurrent callers wait for that one computation); every later request for the
// same version is a lock-free atomic load.
class AnalyticsEngine {
 public:
  AnalyticsEngine(const ReadModel& model, EngineOptions options);

  // Throws sg::common::ApiError(503) until the read model has been bootstrapped.
  std::shared_ptr<const Analytics> current();

  struct Stats {
    std::uint64_t computations;
    std::uint64_t cache_hits;
  };
  [[nodiscard]] Stats stats() const;

 private:
  const ReadModel& model_;
  EngineOptions options_;
  std::atomic<std::shared_ptr<const Analytics>> cache_;
  std::mutex compute_mutex_;
  std::atomic<std::uint64_t> computations_{0};
  std::atomic<std::uint64_t> cache_hits_{0};
};

}  // namespace sg::analytics
