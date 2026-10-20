#include "analytics_engine.hpp"

#include <sg/common/errors.hpp>
#include <sg/common/log.hpp>
#include <sg/common/time.hpp>

#include <algorithm>
#include <chrono>
#include <limits>
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

std::shared_ptr<const std::string> RenderCache::find(const std::string& key) const {
  std::shared_lock lock(mutex_);
  auto it = entries_.find(key);
  return it == entries_.end() ? nullptr : it->second;
}

void RenderCache::insert(const std::string& key, std::shared_ptr<const std::string> body) const {
  std::unique_lock lock(mutex_);
  if (entries_.size() < kMaxEntries) entries_.try_emplace(key, std::move(body));
}

namespace {

// Copies the global metrics of `from` into `to` (whose graph is already built) by user id.
void carry_global_metrics(Analytics& to, const Analytics& from, const std::vector<UserInfo>& users) {
  const std::size_t n = users.size();
  for (auto m : {Metric::pagerank, Metric::betweenness, Metric::closeness}) {
    to.scores[static_cast<std::size_t>(m)].assign(n, 0.0);
  }
  to.betweenness_approximate = from.betweenness_approximate;
  constexpr auto kUnassigned = std::numeric_limits<std::uint32_t>::max();
  auto& membership = to.communities.membership;
  membership.assign(n, kUnassigned);
  std::vector<graph::NodeId> fresh;
  for (std::size_t i = 0; i < n; ++i) {
    const auto old = from.node(users[i].id);
    if (!old) {
      fresh.push_back(static_cast<graph::NodeId>(i));
      continue;
    }
    for (auto m : {Metric::pagerank, Metric::betweenness, Metric::closeness}) {
      to.scores[static_cast<std::size_t>(m)][i] = from.metric(m)[*old];
    }
    membership[i] = from.communities.membership[*old];
  }
  std::uint32_t next_label = static_cast<std::uint32_t>(from.communities.community_count);
  for (auto v : fresh) {
    double best = -1.0;
    std::uint32_t label = kUnassigned;
    const auto neighbors = to.strength_graph.neighbors(v);
    const auto weights = to.strength_graph.weights(v);
    for (std::size_t j = 0; j < neighbors.size(); ++j) {
      const auto u = neighbors[j];
      if (membership[u] != kUnassigned && weights[j] > best) {
        best = weights[j];
        label = membership[u];
      }
    }
    membership[v] = label != kUnassigned ? label : next_label++;
  }
  // Labels stay dense: carried labels are < from.community_count and new ones follow.
  std::vector<bool> used(next_label, false);
  for (auto c : membership) used[c] = true;
  std::vector<std::uint32_t> relabel(next_label, 0);
  std::uint32_t k = 0;
  for (std::uint32_t c = 0; c < next_label; ++c) {
    if (used[c]) relabel[c] = k++;
  }
  for (auto& c : membership) c = relabel[c];
  to.communities.community_count = k;
  to.communities.levels = from.communities.levels;
  to.communities.modularity = graph::modularity(to.graph, membership);
}

}  // namespace

std::shared_ptr<const Analytics> compute_analytics(std::shared_ptr<const GraphState> state,
                                                   const EngineOptions& options, const Analytics* carry_from) {
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
    edges.push_back({result->node_of.at(edge.user_a), result->node_of.at(edge.user_b), edge.strength});
  }
  result->strength_graph = graph::CsrGraph::from_edges(n, edges);
  for (auto& edge : edges) edge.w = graph::louvain_weight(edge.w);
  result->graph = graph::CsrGraph::from_edges(n, edges);
  const auto& g = result->graph;

  if (carry_from == nullptr) {
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
    result->communities = graph::louvain(g);
    result->centrality_version = state->version;
  } else {
    carry_global_metrics(*result, *carry_from, users);
    result->full = false;
    result->centrality_version = carry_from->centrality_version;
    result->centrality_computed_at = carry_from->centrality_computed_at;
  }
  result->scores[static_cast<std::size_t>(Metric::degree)] = graph::degree_centrality(g);
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
  if (result->full) result->centrality_computed_at = result->computed_at;
  result->compute_ms =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
  return result;
}

AnalyticsEngine::AnalyticsEngine(const ReadModel& model, EngineOptions options)
    : model_(model), options_(options) {}

AnalyticsEngine::~AnalyticsEngine() { stop(); }

void AnalyticsEngine::start_background_refresh() {
  if (background_.exchange(true)) return;
  refresher_ = std::jthread([this](const std::stop_token& stop) { refresh_loop(stop); });
}

void AnalyticsEngine::stop() {
  if (refresher_.joinable()) {
    refresher_.request_stop();
    refresher_.join();
  }
  background_ = false;
}

void AnalyticsEngine::notify_changed() {
  if (!background_) {
    if (model_.ready()) refresh();
    return;
  }
  {
    std::lock_guard lock(signal_mutex_);
    dirty_ = true;
  }
  changed_.notify_one();
}

void AnalyticsEngine::refresh_loop(const std::stop_token& stop) {
  auto last = std::chrono::steady_clock::now() - options_.min_refresh_interval;
  while (!stop.stop_requested()) {
    bool upgrade = false;  // no new writes, but the global metrics are due for a full computation
    {
      std::unique_lock lock(signal_mutex_);
      const auto snapshot = cache_.load();
      const bool carried = snapshot && !snapshot->full;
      if (carried) {
        const auto due = last_full_.load() + options_.full_refresh_interval;
        upgrade = !changed_.wait_until(lock, stop, due, [this] { return dirty_; });
      } else {
        changed_.wait(lock, stop, [this] { return dirty_; });
      }
      if (stop.stop_requested()) return;
      if (!upgrade) {
        // Coalesce: everything that arrives before the interval has passed goes into one computation.
        changed_.wait_until(lock, stop, last + options_.min_refresh_interval, [] { return false; });
        if (stop.stop_requested()) return;
      }
      dirty_ = false;
    }
    last = std::chrono::steady_clock::now();
    try {
      if (model_.ready()) refresh(upgrade);
    } catch (const std::exception& e) {
      sg::common::log_error("analytics refresh failed", {{"error", e.what()}});
    }
  }
}

std::shared_ptr<const Analytics> AnalyticsEngine::current() {
  if (!model_.ready()) throw sg::common::unavailable("Analytics read model is still loading");

  auto cached = cache_.load();
  if (cached && cached->version() == model_.version()) {
    cache_hits_.fetch_add(1, std::memory_order_relaxed);
    return cached;
  }
  if (cached && background_) {
    stale_reads_.fetch_add(1, std::memory_order_relaxed);
    return cached;
  }
  return refresh();
}

std::shared_ptr<const Analytics> AnalyticsEngine::wait_for_version(std::uint64_t version,
                                                                   std::chrono::milliseconds timeout) {
  if (!background_) {
    // No refresher: wait for the read model, then compute on demand.
    (void)model_.wait_for_version(version, timeout);
    return current();
  }
  auto latest = current();
  if (latest->version() >= version) return latest;
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  std::unique_lock lock(signal_mutex_);
  published_.wait_until(lock, deadline, [&] {
    latest = cache_.load();
    return latest->version() >= version;
  });
  return latest;
}

std::shared_ptr<const Analytics> AnalyticsEngine::refresh(bool force_full) {
  std::lock_guard lock(compute_mutex_);
  // Another thread may have finished the computation while we waited for the lock.
  auto cached = cache_.load();
  auto state = model_.materialize();
  if (cached && cached->version() == state->version && (cached->full || !force_full)) {
    cache_hits_.fetch_add(1, std::memory_order_relaxed);
    return cached;
  }
  const auto now = std::chrono::steady_clock::now();
  const bool full = force_full || !cached || options_.full_refresh_interval.count() <= 0 ||
                    now - last_full_.load() >= options_.full_refresh_interval;
  auto fresh = compute_analytics(std::move(state), options_, full ? nullptr : cached.get());
  computations_.fetch_add(1, std::memory_order_relaxed);
  if (full) {
    full_computations_.fetch_add(1, std::memory_order_relaxed);
    last_full_ = now;
  }
  cache_.store(fresh);
  {
    std::lock_guard signal(signal_mutex_);
  }
  published_.notify_all();
  return fresh;
}

AnalyticsEngine::Stats AnalyticsEngine::stats() const {
  return {computations_.load(std::memory_order_relaxed), cache_hits_.load(std::memory_order_relaxed),
          stale_reads_.load(std::memory_order_relaxed), full_computations_.load(std::memory_order_relaxed)};
}

}  // namespace sg::analytics
