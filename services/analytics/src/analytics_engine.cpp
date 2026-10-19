#include "analytics_engine.hpp"

#include <sg/common/errors.hpp>
#include <sg/common/time.hpp>

#include <algorithm>
#include <chrono>
#include <numeric>

namespace sg::analytics {

namespace graph = sg::graph;

std::optional<Metric> parse_metric(std::string_view name) {
  if (name == "pagerank") return Metric::pagerank;
  if (name == "betweenness") return Metric::betweenness;
  if (name == "closeness") return Metric::closeness;
  if (name == "degree") return Metric::degree;
  return std::nullopt;
}

std::string_view metric_name(Metric metric) {
  switch (metric) {
    case Metric::pagerank: return "pagerank";
    case Metric::betweenness: return "betweenness";
    case Metric::closeness: return "closeness";
    case Metric::degree: return "degree";
  }
  return "unknown";
}

std::optional<graph::NodeId> Analytics::node(std::int64_t user_id) const {
  auto it = node_of.find(user_id);
  if (it == node_of.end()) return std::nullopt;
  return it->second;
}

std::shared_ptr<const Analytics> compute_analytics(std::shared_ptr<const GraphState> state,
                                                   const EngineOptions& options) {
  const auto started = std::chrono::steady_clock::now();
  auto result = std::make_shared<Analytics>();
  const auto& users = state->users;
  const std::size_t n = users.size();

  // Affinity only compares attributes for equality, so ids just need to be consistent within this
  // snapshot: intern the category/branch names (the taxonomy is data-driven, contracts §10.3).
  std::unordered_map<std::string, std::uint16_t> category_ids;
  std::unordered_map<std::string, std::uint16_t> branch_ids;
  auto intern = [](std::unordered_map<std::string, std::uint16_t>& ids, const std::string& name) {
    return ids.try_emplace(name, static_cast<std::uint16_t>(ids.size())).first->second;
  };
  result->node_of.reserve(n);
  result->profiles.reserve(n);
  for (std::size_t i = 0; i < n; ++i) {
    result->node_of.emplace(users[i].id, static_cast<graph::NodeId>(i));
    result->profiles.push_back(graph::Profile{intern(category_ids, users[i].category),
                                              intern(branch_ids, users[i].branch), users[i].influence});
  }

  std::vector<graph::WeightedEdge> edges;
  edges.reserve(state->edges.size());
  for (const auto& edge : state->edges) {
    edges.push_back({result->node_of.at(edge.user_a), result->node_of.at(edge.user_b),
                     graph::louvain_weight(edge.strength)});
  }
  result->graph = graph::CsrGraph::from_edges(n, edges);
  const auto& g = result->graph;

  result->scores[static_cast<std::size_t>(Metric::pagerank)] = graph::pagerank(g).scores;

  graph::BetweennessOptions betweenness_options;
  betweenness_options.threads = options.threads;
  if (n > options.exact_betweenness_max_nodes) {
    betweenness_options.sample_sources = options.betweenness_samples;
  }
  auto betweenness = graph::betweenness(g, betweenness_options);
  result->betweenness_approximate = betweenness.approximate;
  result->scores[static_cast<std::size_t>(Metric::betweenness)] = std::move(betweenness.scores);

  result->scores[static_cast<std::size_t>(Metric::closeness)] = graph::closeness(g, options.threads);
  result->scores[static_cast<std::size_t>(Metric::degree)] = graph::degree_centrality(g);
  result->communities = graph::louvain(g);
  result->components = graph::connected_components(g);

  for (std::size_t m = 0; m < 4; ++m) {
    const auto& score = result->scores[m];
    auto& order = result->order[m];
    order.resize(n);
    std::iota(order.begin(), order.end(), graph::NodeId{0});
    // Highest score first; ties broken by smaller user id (node order == id order).
    std::ranges::stable_sort(order, [&](graph::NodeId a, graph::NodeId b) { return score[a] > score[b]; });
    auto& rank = result->ranks[m];
    rank.resize(n);
    for (std::size_t r = 0; r < n; ++r) rank[order[r]] = static_cast<std::uint32_t>(r + 1);
  }

  result->state = std::move(state);
  result->computed_at = sg::common::to_rfc3339(std::chrono::system_clock::now());
  result->compute_ms =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
  return result;
}

AnalyticsEngine::AnalyticsEngine(const ReadModel& model, EngineOptions options)
    : model_(model), options_(options) {}

std::shared_ptr<const Analytics> AnalyticsEngine::current() {
  if (!model_.ready()) throw sg::common::unavailable("Analytics read model is still loading");

  auto cached = cache_.load();
  if (cached && cached->version() == model_.version()) {
    cache_hits_.fetch_add(1, std::memory_order_relaxed);
    return cached;
  }

  std::lock_guard lock(compute_mutex_);
  // Another thread may have finished the computation while we waited for the lock.
  cached = cache_.load();
  auto state = model_.materialize();
  if (cached && cached->version() == state->version) {
    cache_hits_.fetch_add(1, std::memory_order_relaxed);
    return cached;
  }
  auto fresh = compute_analytics(std::move(state), options_);
  computations_.fetch_add(1, std::memory_order_relaxed);
  cache_.store(fresh);
  return fresh;
}

AnalyticsEngine::Stats AnalyticsEngine::stats() const {
  return {computations_.load(std::memory_order_relaxed), cache_hits_.load(std::memory_order_relaxed)};
}

}  // namespace sg::analytics
