#pragma once

#include "read_model.hpp"

#include <sg/graph/graphcore.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
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
  // Background refresh: minimum time between two recomputations. Bursts of writes inside this window
  // are coalesced into one recomputation, which bounds the CPU analytics takes from request serving
  // at the cost of up to this much extra staleness.
  std::chrono::milliseconds min_refresh_interval{0};
  // Two-tier refresh. The global metrics (PageRank, betweenness, closeness, Louvain communities) cost
  // O(k·E) and take most of a second on the full dataset; recomputing them after every burst of writes
  // keeps CPU cores busy that should serve requests. Within this interval after a full computation, a
  // refresh only rebuilds the graph structure and the cheap metrics (adjacency, degree, components,
  // ranks) and carries the global scores over by user id; a full computation runs once the interval
  // has passed. 0 = every refresh is full.
  std::chrono::milliseconds full_refresh_interval{0};
};

// Serialized responses of one immutable snapshot, keyed by route and canonical parameters. Because
// the snapshot never changes, an entry can never go stale; it is dropped together with the snapshot
// when a newer version is published. Bounded: once full, further responses are simply not cached.
class RenderCache {
 public:
  static constexpr std::size_t kMaxEntries = 65536;
  [[nodiscard]] std::shared_ptr<const std::string> find(const std::string& key) const;
  void insert(const std::string& key, std::shared_ptr<const std::string> body) const;

 private:
  mutable std::shared_mutex mutex_;
  mutable std::unordered_map<std::string, std::shared_ptr<const std::string>> entries_;
};

// Everything derived from one GraphState. Immutable once built, shared between request threads.
struct Analytics {
  std::shared_ptr<const GraphState> state;
  std::unordered_map<std::int64_t, sg::graph::NodeId> node_of;  // user id -> dense node index
  std::vector<sg::graph::Profile> profiles;
  sg::graph::CsrGraph graph;           // weights = louvain_weight(strength)
  sg::graph::CsrGraph strength_graph;  // same topology, weights = connection strength (for output)
  std::array<std::vector<double>, 4> scores;          // indexed by Metric
  std::array<std::vector<std::uint32_t>, 4> ranks;    // 1-based rank per node, indexed by Metric
  std::array<std::vector<sg::graph::NodeId>, 4> order; // nodes sorted by rank, indexed by Metric
  bool betweenness_approximate = false;
  sg::graph::CommunityResult communities;
  sg::graph::ComponentsResult components;
  std::string computed_at;
  double compute_ms = 0.0;
  bool full = true;                       // false: global metrics carried over from an earlier snapshot
  std::uint64_t centrality_version = 0;   // graph version the global metrics were computed at
  std::string centrality_computed_at;
  RenderCache rendered;

  [[nodiscard]] std::uint64_t version() const { return state->version; }
  [[nodiscard]] std::optional<sg::graph::NodeId> node(std::int64_t user_id) const;
  [[nodiscard]] const UserInfo& user(sg::graph::NodeId node) const { return state->users[node]; }
  [[nodiscard]] const std::vector<double>& metric(Metric m) const { return scores[static_cast<std::size_t>(m)]; }
};

// Computes all analytics for a graph state. Pure function: no locks, no I/O. Complexity is dominated
// by Brandes betweenness, O(V·E) exact or O(k·E) sampled.
//
// With `carry_from`, only the structure and the cheap metrics are computed (O(V log V + E)); PageRank,
// betweenness, closeness and community membership are copied from `carry_from` by user id. Users that
// are new since then score 0 and join the community of their strongest carried-over neighbour (or a
// community of their own); modularity is re-evaluated for the current graph.
std::shared_ptr<const Analytics> compute_analytics(std::shared_ptr<const GraphState> state,
                                                   const EngineOptions& options,
                                                   const Analytics* carry_from = nullptr);

// Publishes analytics snapshots of the read model. Reads are a lock-free atomic load of the latest
// published snapshot.
//
// Without background refresh, the first read after a change computes the new snapshot (single-flight:
// concurrent callers wait for that one computation). With start_background_refresh(), requests never
// wait for a computation: they are served the latest published snapshot (stale-while-revalidate) while
// a background thread recomputes whenever the read model moves on, coalescing bursts of writes.
// Callers that need their own write reflected use wait_for_version().
class AnalyticsEngine {
 public:
  AnalyticsEngine(const ReadModel& model, EngineOptions options);
  ~AnalyticsEngine();
  AnalyticsEngine(const AnalyticsEngine&) = delete;
  AnalyticsEngine& operator=(const AnalyticsEngine&) = delete;

  void start_background_refresh();
  void stop();

  // Tells the engine the read model changed: wakes the background refresher, or recomputes now when
  // there is none.
  void notify_changed();

  // Latest snapshot. Throws sg::common::ApiError(503) until the read model has been bootstrapped.
  std::shared_ptr<const Analytics> current();

  // A snapshot of at least `version`, waiting up to `timeout` for it to be published; returns the
  // latest snapshot (possibly older) when the wait times out.
  std::shared_ptr<const Analytics> wait_for_version(std::uint64_t version, std::chrono::milliseconds timeout);

  struct Stats {
    std::uint64_t computations;
    std::uint64_t cache_hits;
    std::uint64_t stale_reads;  // served while a newer read-model version was being computed
    std::uint64_t full_computations;  // computations that recomputed the global metrics
  };
  [[nodiscard]] Stats stats() const;

 private:
  std::shared_ptr<const Analytics> refresh(bool force_full = false);
  void refresh_loop(const std::stop_token& stop);

  const ReadModel& model_;
  EngineOptions options_;
  std::atomic<std::shared_ptr<const Analytics>> cache_;
  std::mutex compute_mutex_;
  std::mutex signal_mutex_;
  std::condition_variable_any changed_;    // read model moved on (wakes the refresher)
  std::condition_variable_any published_;  // a new snapshot was published (wakes waiters)
  bool dirty_ = false;
  std::atomic<bool> background_{false};
  std::jthread refresher_;
  std::atomic<std::uint64_t> computations_{0};
  std::atomic<std::uint64_t> cache_hits_{0};
  std::atomic<std::uint64_t> stale_reads_{0};
  std::atomic<std::uint64_t> full_computations_{0};
  std::atomic<std::chrono::steady_clock::time_point> last_full_{};
};

}  // namespace sg::analytics
