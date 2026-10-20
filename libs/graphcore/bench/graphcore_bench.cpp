// graphcore_bench: Google Benchmark suite for sg::graphcore plus the legacy baseline.
//
//   graphcore_bench [google-benchmark flags]   run the benchmarks
//   graphcore_bench --stats                    print statistics of the benchmark graphs
//
// Graphs: the real Stack Overflow 2008 interaction graph (data/stackoverflow, override the
// directory with $SG_DATASET_DIR) and synthetic homophilous graphs of 1k, 10k and 100k nodes.
// Exact O(V·E) algorithms are skipped where one run would exceed ~30 s; the legacy O(V·E) loops
// are timed on a prefix of the sources and reported per source (see the "per_source" counter).

#include <benchmark/benchmark.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <numeric>
#include <string>
#include <vector>

#include "dataset.hpp"
#include "legacy_baseline.hpp"
#include "sg/graph/graphcore.hpp"

namespace {

namespace graph = sg::graph;
using sg::bench::BenchGraph;

// ---- graph cache --------------------------------------------------------------------------------

struct Prepared {
  BenchGraph data;
  graph::CsrGraph louvain_graph;            // weights = louvain_weight(strength)
  std::vector<std::uint32_t> membership;    // Louvain communities (input of recommend)
  std::vector<std::pair<graph::NodeId, graph::NodeId>> queries;  // random (from, to) pairs
  std::vector<graph::NodeId> users;                              // random recommendation users
};

std::map<std::string, std::unique_ptr<Prepared>>& cache() {
  static std::map<std::string, std::unique_ptr<Prepared>> instance;
  return instance;
}

std::string load_error;  // why the real graph is unavailable (empty when it loaded)

const Prepared* prepared(const std::string& key) {
  auto& entries = cache();
  if (const auto it = entries.find(key); it != entries.end()) {
    return it->second.get();
  }
  std::optional<BenchGraph> data;
  if (key == "so") {
    data = sg::bench::load_stackoverflow(sg::bench::dataset_dir(), load_error);
  } else {
    data = sg::bench::make_synthetic(static_cast<std::size_t>(std::stoul(key)));
  }
  if (!data) {
    entries.emplace(key, nullptr);
    return nullptr;
  }
  auto p = std::make_unique<Prepared>();
  p->data = std::move(*data);
  p->louvain_graph = p->data.graph.map_weights(graph::louvain_weight);
  p->membership = graph::louvain(p->louvain_graph).membership;
  const std::size_t n = p->data.graph.node_count();
  graph::SplitMix64 rng(4242);
  for (int i = 0; i < 4096; ++i) {
    p->queries.emplace_back(static_cast<graph::NodeId>(rng.bounded(n)), static_cast<graph::NodeId>(rng.bounded(n)));
  }
  for (int i = 0; i < 256; ++i) {
    p->users.push_back(static_cast<graph::NodeId>(rng.bounded(n)));
  }
  return entries.emplace(key, std::move(p)).first->second.get();
}

/// Fetches the graph or marks the benchmark skipped.
const Prepared* require(benchmark::State& state, const std::string& key) {
  const Prepared* p = prepared(key);
  if (p == nullptr) {
    state.SkipWithError(("dataset unavailable: " + load_error).c_str());
  }
  return p;
}

void set_graph_counters(benchmark::State& state, const Prepared& p) {
  state.counters["nodes"] = static_cast<double>(p.data.graph.node_count());
  state.counters["edges"] = static_cast<double>(p.data.graph.edge_count());
}

/// Counter shown as time per unit of work (e.g. per BFS source), averaged over iterations.
benchmark::Counter per_unit(double units) {
  return benchmark::Counter(units, benchmark::Counter::kIsIterationInvariantRate | benchmark::Counter::kInvert);
}

// ---- new library ----------------------------------------------------------------------------------

/// Streams users.csv.gz + connections.csv.gz through zlib and builds the CSR graph.
void bm_dataset_load(benchmark::State& state) {
  std::string error;
  std::size_t nodes = 0;
  std::size_t edges = 0;
  for (auto _ : state) {
    auto data = sg::bench::load_stackoverflow(sg::bench::dataset_dir(), error);
    if (!data) {
      state.SkipWithError(("dataset unavailable: " + error).c_str());
      break;
    }
    nodes = data->graph.node_count();
    edges = data->graph.edge_count();
    benchmark::DoNotOptimize(data);
  }
  state.counters["nodes"] = static_cast<double>(nodes);
  state.counters["edges"] = static_cast<double>(edges);
}

void bm_csr_build(benchmark::State& state, const std::string& key) {
  const Prepared* p = require(state, key);
  if (p == nullptr) return;
  for (auto _ : state) {
    auto g = graph::CsrGraph::from_edges(p->data.graph.node_count(), p->data.edges);
    benchmark::DoNotOptimize(g);
  }
  set_graph_counters(state, *p);
}

void bm_pagerank(benchmark::State& state, const std::string& key) {
  const Prepared* p = require(state, key);
  if (p == nullptr) return;
  int iterations = 0;
  for (auto _ : state) {
    auto r = graph::pagerank(p->data.graph);
    iterations = r.iterations;
    benchmark::DoNotOptimize(r);
  }
  set_graph_counters(state, *p);
  state.counters["iterations"] = iterations;
  state.counters["per_iteration"] = per_unit(iterations);
}

void bm_betweenness(benchmark::State& state, const std::string& key, unsigned threads, std::size_t sample) {
  const Prepared* p = require(state, key);
  if (p == nullptr) return;
  graph::BetweennessOptions options;
  options.threads = threads;
  options.sample_sources = sample;
  std::size_t sources = 0;
  for (auto _ : state) {
    auto r = graph::betweenness(p->data.graph, options);
    sources = r.sources_used;
    benchmark::DoNotOptimize(r);
  }
  set_graph_counters(state, *p);
  state.counters["threads"] = threads;
  state.counters["sources"] = static_cast<double>(sources);
  state.counters["per_source"] = per_unit(static_cast<double>(sources));
}

void bm_closeness(benchmark::State& state, const std::string& key, unsigned threads) {
  const Prepared* p = require(state, key);
  if (p == nullptr) return;
  for (auto _ : state) {
    auto r = graph::closeness(p->data.graph, threads);
    benchmark::DoNotOptimize(r);
  }
  set_graph_counters(state, *p);
  state.counters["threads"] = threads;
  state.counters["per_source"] = per_unit(static_cast<double>(p->data.graph.node_count()));
}

void bm_louvain(benchmark::State& state, const std::string& key) {
  const Prepared* p = require(state, key);
  if (p == nullptr) return;
  graph::CommunityResult r;
  for (auto _ : state) {
    r = graph::louvain(p->louvain_graph);
    benchmark::DoNotOptimize(r);
  }
  set_graph_counters(state, *p);
  state.counters["communities"] = static_cast<double>(r.community_count);
  state.counters["modularity"] = r.modularity;
  state.counters["levels"] = r.levels;
}

void bm_recommend(benchmark::State& state, const std::string& key) {
  const Prepared* p = require(state, key);
  if (p == nullptr) return;
  std::size_t i = 0;
  for (auto _ : state) {
    auto r = graph::recommend(p->data.graph, p->data.profiles, p->membership, p->users[i++ % p->users.size()]);
    benchmark::DoNotOptimize(r);
  }
  set_graph_counters(state, *p);
}

void bm_shortest_path(benchmark::State& state, const std::string& key) {
  const Prepared* p = require(state, key);
  if (p == nullptr) return;
  std::size_t i = 0;
  for (auto _ : state) {
    const auto& [from, to] = p->queries[i++ % p->queries.size()];
    auto r = graph::shortest_path(p->data.graph, from, to);
    benchmark::DoNotOptimize(r);
  }
  set_graph_counters(state, *p);
}

void bm_components(benchmark::State& state, const std::string& key) {
  const Prepared* p = require(state, key);
  if (p == nullptr) return;
  for (auto _ : state) {
    auto r = graph::connected_components(p->data.graph);
    benchmark::DoNotOptimize(r);
  }
  set_graph_counters(state, *p);
}

void bm_bfs_distances(benchmark::State& state, const std::string& key) {
  const Prepared* p = require(state, key);
  if (p == nullptr) return;
  std::size_t i = 0;
  for (auto _ : state) {
    auto r = graph::bfs_distances(p->data.graph, p->users[i++ % p->users.size()]);
    benchmark::DoNotOptimize(r);
  }
  set_graph_counters(state, *p);
}

// ---- legacy baseline ----------------------------------------------------------------------------

void bm_legacy_build(benchmark::State& state, const std::string& key) {
  const Prepared* p = require(state, key);
  if (p == nullptr) return;
  for (auto _ : state) {
    sg::bench::legacy::ConnectionManager cm(p->data);
    benchmark::DoNotOptimize(cm);
  }
  set_graph_counters(state, *p);
}

void bm_legacy_pagerank(benchmark::State& state, const std::string& key) {
  const Prepared* p = require(state, key);
  if (p == nullptr) return;
  sg::bench::legacy::ConnectionManager cm(p->data);
  int iterations = 0;
  for (auto _ : state) {
    auto r = cm.calculatePageRank(&iterations);
    benchmark::DoNotOptimize(r);
  }
  set_graph_counters(state, *p);
  state.counters["iterations"] = iterations;
  state.counters["per_iteration"] = per_unit(iterations);
}

void bm_legacy_betweenness(benchmark::State& state, const std::string& key, std::size_t sources) {
  const Prepared* p = require(state, key);
  if (p == nullptr) return;
  sg::bench::legacy::ConnectionManager cm(p->data);
  const std::size_t used = std::min(sources, cm.user_count());
  for (auto _ : state) {
    auto r = cm.calculateBetweennessCentrality(used);
    benchmark::DoNotOptimize(r);
  }
  set_graph_counters(state, *p);
  state.counters["sources"] = static_cast<double>(used);
  state.counters["per_source"] = per_unit(static_cast<double>(used));
}

void bm_legacy_closeness(benchmark::State& state, const std::string& key, std::size_t sources) {
  const Prepared* p = require(state, key);
  if (p == nullptr) return;
  sg::bench::legacy::ConnectionManager cm(p->data);
  const std::size_t used = std::min(sources, cm.user_count());
  for (auto _ : state) {
    auto r = cm.calculateClosenessCentrality(used);
    benchmark::DoNotOptimize(r);
  }
  set_graph_counters(state, *p);
  state.counters["sources"] = static_cast<double>(used);
  state.counters["per_source"] = per_unit(static_cast<double>(used));
}

void bm_legacy_shortest_path(benchmark::State& state, const std::string& key) {
  const Prepared* p = require(state, key);
  if (p == nullptr) return;
  sg::bench::legacy::ConnectionManager cm(p->data);
  std::size_t i = 0;
  for (auto _ : state) {
    const auto& [from, to] = p->queries[i++ % p->queries.size()];
    auto r = cm.bfsPath(p->data.external_ids[from], p->data.external_ids[to]);
    benchmark::DoNotOptimize(r);
  }
  set_graph_counters(state, *p);
}

// ---- registration ---------------------------------------------------------------------------------

struct Size {
  std::string key;    // cache key: "so" or the node count
  std::string label;  // benchmark name component
};

void register_all() {
  const std::vector<Size> sizes = {{"so", "so2008"}, {"1000", "1k"}, {"10000", "10k"}, {"100000", "100k"}};
  auto reg = [](const std::string& name, auto fn) {
    return benchmark::RegisterBenchmark(name.c_str(), fn)->UseRealTime();
  };
  for (const Size& s : sizes) {
    const std::string k = s.key;
    const bool huge = s.label == "100k";
    if (s.label == "so2008") {
      reg("dataset_load/so2008", [](benchmark::State& st) { bm_dataset_load(st); })->Unit(benchmark::kMillisecond);
    }
    reg("csr_build/" + s.label, [k](benchmark::State& st) { bm_csr_build(st, k); })->Unit(benchmark::kMillisecond);
    reg("pagerank/" + s.label, [k](benchmark::State& st) { bm_pagerank(st, k); })->Unit(benchmark::kMillisecond);
    // Exact Brandes is O(V·E) and one run must stay under ~30 s: at 100k nodes it would take
    // well over an hour, and on the real graph one thread takes ~65 s (its per-source cost is
    // measured by the sampled single-thread run), so only the 4-thread run is registered there.
    std::vector<unsigned> exact_threads;
    if (s.label == "1k" || s.label == "10k") {
      exact_threads = {1U, 2U, 4U};
    } else if (s.label == "so2008") {
      exact_threads = {4U};
    }
    for (const unsigned t : exact_threads) {
      reg("betweenness_exact/" + s.label + "/threads:" + std::to_string(t),
          [k, t](benchmark::State& st) { bm_betweenness(st, k, t, 0); })
          ->Unit(benchmark::kMillisecond);
    }
    if (s.label != "1k") {
      for (const unsigned t : {1U, 2U, 4U}) {
        reg("betweenness_sampled_k256/" + s.label + "/threads:" + std::to_string(t),
            [k, t](benchmark::State& st) { bm_betweenness(st, k, t, 256); })
            ->Unit(benchmark::kMillisecond);
      }
    }
    for (const unsigned t : {1U, 4U}) {
      if (huge && t == 1) {
        continue;
      }
      reg("closeness/" + s.label + "/threads:" + std::to_string(t), [k, t](benchmark::State& st) { bm_closeness(st, k, t); })
          ->Unit(benchmark::kMillisecond);
    }
    reg("louvain/" + s.label, [k](benchmark::State& st) { bm_louvain(st, k); })->Unit(benchmark::kMillisecond);
    reg("recommend/" + s.label, [k](benchmark::State& st) { bm_recommend(st, k); })->Unit(benchmark::kMicrosecond);
    reg("shortest_path/" + s.label, [k](benchmark::State& st) { bm_shortest_path(st, k); })->Unit(benchmark::kMicrosecond);
    reg("bfs_distances/" + s.label, [k](benchmark::State& st) { bm_bfs_distances(st, k); })->Unit(benchmark::kMicrosecond);
    reg("components/" + s.label, [k](benchmark::State& st) { bm_components(st, k); })->Unit(benchmark::kMicrosecond);

    // Legacy baseline on the same graphs. Its O(V·E) loops are timed on a prefix of the sources.
    const std::size_t legacy_sources = s.label == "1k" ? 1000 : (s.label == "so2008" ? 32 : (huge ? 8 : 100));
    reg("legacy/build/" + s.label, [k](benchmark::State& st) { bm_legacy_build(st, k); })->Unit(benchmark::kMillisecond);
    reg("legacy/pagerank/" + s.label, [k](benchmark::State& st) { bm_legacy_pagerank(st, k); })->Unit(benchmark::kMillisecond);
    reg("legacy/betweenness/" + s.label + "/sources:" + std::to_string(legacy_sources),
        [k, legacy_sources](benchmark::State& st) { bm_legacy_betweenness(st, k, legacy_sources); })
        ->Unit(benchmark::kMillisecond);
    reg("legacy/closeness/" + s.label + "/sources:" + std::to_string(legacy_sources),
        [k, legacy_sources](benchmark::State& st) { bm_legacy_closeness(st, k, legacy_sources); })
        ->Unit(benchmark::kMillisecond);
    reg("legacy/shortest_path/" + s.label, [k](benchmark::State& st) { bm_legacy_shortest_path(st, k); })
        ->Unit(benchmark::kMillisecond);
  }
}

// ---- --stats ----------------------------------------------------------------------------------------

double percentile(std::vector<std::size_t> values, double q) {
  if (values.empty()) return 0.0;
  std::sort(values.begin(), values.end());
  const auto index = static_cast<std::size_t>(std::floor(q * static_cast<double>(values.size() - 1)));
  return static_cast<double>(values[index]);
}

void print_stats(const std::string& key) {
  const Prepared* p = prepared(key);
  if (p == nullptr) {
    std::printf("%s: unavailable (%s)\n", key.c_str(), load_error.c_str());
    return;
  }
  const graph::CsrGraph& g = p->data.graph;
  const std::size_t n = g.node_count();
  std::vector<std::size_t> degrees(n);
  std::size_t isolated = 0;
  for (graph::NodeId u = 0; u < n; ++u) {
    degrees[u] = g.degree(u);
    isolated += degrees[u] == 0 ? 1 : 0;
  }
  const double mean_degree = 2.0 * static_cast<double>(g.edge_count()) / static_cast<double>(n);
  double strength_sum = 0.0;
  double strength_min = 1e300;
  double strength_max = -1e300;
  for (const auto& e : g.edge_list()) {
    strength_sum += e.w;
    strength_min = std::min(strength_min, e.w);
    strength_max = std::max(strength_max, e.w);
  }
  const auto components = graph::connected_components(g);
  const auto communities = graph::louvain(p->louvain_graph);
  const auto rank = graph::pagerank(g);

  // Distances: exact eccentricity-free estimate from 256 random BFS sources.
  graph::SplitMix64 rng(7);
  std::size_t pairs = 0;
  double distance_sum = 0.0;
  std::int32_t farthest = 0;
  for (int i = 0; i < 256; ++i) {
    const auto source = static_cast<graph::NodeId>(rng.bounded(n));
    for (const std::int32_t d : graph::bfs_distances(g, source)) {
      if (d > 0) {
        ++pairs;
        distance_sum += d;
        farthest = std::max(farthest, d);
      }
    }
  }
  // Degree histogram in powers of two.
  std::map<std::size_t, std::size_t> buckets;
  for (const std::size_t d : degrees) {
    std::size_t upper = 1;
    while (upper < d) upper <<= 1U;
    ++buckets[d == 0 ? 0 : upper];
  }

  std::printf("graph %s\n", p->data.name.c_str());
  std::printf("  nodes %zu, edges %zu, density %.6f, isolated nodes %zu\n", n, g.edge_count(),
              2.0 * static_cast<double>(g.edge_count()) / (static_cast<double>(n) * static_cast<double>(n - 1)), isolated);
  std::printf("  degree: min %.0f, median %.0f, mean %.2f, p90 %.0f, p99 %.0f, max %zu\n", percentile(degrees, 0.0),
              percentile(degrees, 0.5), mean_degree, percentile(degrees, 0.9), percentile(degrees, 0.99), g.max_degree());
  std::printf("  degree histogram (<= upper bound: nodes):");
  for (const auto& [upper, count] : buckets) {
    std::printf(" %zu:%zu", upper, count);
  }
  std::printf("\n  edge weight (strength): min %.3f, mean %.3f, max %.3f\n", strength_min,
              strength_sum / static_cast<double>(g.edge_count()), strength_max);
  std::printf("  components %zu, largest %zu (%.2f%%)\n", components.count, components.largest,
              100.0 * static_cast<double>(components.largest) / static_cast<double>(n));
  std::printf("  BFS from 256 random sources: mean distance %.3f, max distance seen %d\n",
              distance_sum / static_cast<double>(pairs), farthest);
  std::printf("  louvain (weights 0.5+0.5*strength): %zu communities, modularity %.4f, levels %d\n",
              communities.community_count, communities.modularity, communities.levels);
  std::printf("  pagerank: %d iterations, converged %s\n", rank.iterations, rank.converged ? "yes" : "no");
  std::vector<std::size_t> order(n);
  std::iota(order.begin(), order.end(), std::size_t{0});
  std::partial_sort(order.begin(), order.begin() + 5, order.end(),
                    [&](std::size_t a, std::size_t b) { return rank.scores[a] > rank.scores[b]; });
  std::printf("  top-5 pagerank ids:");
  for (int i = 0; i < 5; ++i) {
    std::printf(" %s(%.5f, deg %zu)", p->data.external_ids[order[static_cast<std::size_t>(i)]].c_str(),
                rank.scores[order[static_cast<std::size_t>(i)]], degrees[order[static_cast<std::size_t>(i)]]);
  }
  std::printf("\n");
}

}  // namespace

int main(int argc, char** argv) {
  if (argc > 1 && std::strcmp(argv[1], "--stats") == 0) {
    std::printf("dataset dir: %s (zlib %s)\n", sg::bench::dataset_dir().c_str(), sg::bench::has_zlib() ? "yes" : "no");
    for (const char* key : {"so", "1000", "10000", "100000"}) {
      print_stats(key);
    }
    return 0;
  }
  register_all();
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  benchmark::AddCustomContext("dataset_dir", sg::bench::dataset_dir());
  benchmark::AddCustomContext("zlib", sg::bench::has_zlib() ? "yes" : "no");
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return 0;
}
