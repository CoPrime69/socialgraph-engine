#include "sg/graph/centrality.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <numeric>
#include <stdexcept>

#include "sg/graph/generator.hpp"
#include "sg/graph/parallel.hpp"

namespace sg::graph {

namespace {

/// Below roughly this many elementary steps per thread, starting a thread costs more than the
/// work it takes over (only used when the caller asks for threads == 0).
constexpr double kMinWorkPerThread = 1.0e6;

/// Threads to use: an explicit request is honoured; 0 means hardware concurrency, reduced so
/// that every thread gets at least kMinWorkPerThread steps. Never more than `max_tasks`.
unsigned choose_threads(unsigned requested, double work, std::size_t max_tasks) {
  unsigned threads = resolve_thread_count(requested);
  if (requested == 0) {
    const double useful = std::max(1.0, std::floor(work / kMinWorkPerThread));
    if (useful < static_cast<double>(threads)) {
      threads = static_cast<unsigned>(useful);
    }
  }
  return static_cast<unsigned>(std::min<std::size_t>(threads, std::max<std::size_t>(max_tasks, 1)));
}

// ---------------------------------------------------------------------------------------------
// Brandes betweenness
// ---------------------------------------------------------------------------------------------

/// Per-node state of one single-source pass (16 bytes, one cache access per neighbour).
/// `value` holds σ (number of shortest paths from the source) during the forward pass; the
/// backward pass overwrites it with the coefficient (1 + δ)/σ once the node's dependency δ is
/// known, which is all its predecessors need from it.
struct BrandesNode {
  double value;
  std::int32_t dist;  // BFS distance, -1 = not reached
};

/// Scratch of one worker: reused for every source it processes, reset in O(reached).
class BrandesWorker {
 public:
  explicit BrandesWorker(std::size_t n) : state_(n, BrandesNode{0.0, -1}), order_(n) {}

  /// Adds the dependencies δ_s(v) of source `s` to acc[v] for every v != s.
  void accumulate(const std::size_t* offsets, const NodeId* adjacency, NodeId s, double* acc) {
    BrandesNode* st = state_.data();
    NodeId* order = order_.data();
    std::size_t tail = 0;

    // Forward: BFS counting shortest paths; `order` doubles as the queue and the stack.
    st[s].dist = 0;
    st[s].value = 1.0;
    order[tail++] = s;
    for (std::size_t head = 0; head < tail; ++head) {
      const NodeId v = order[head];
      const std::int32_t next_dist = st[v].dist + 1;
      const double sigma_v = st[v].value;
      for (std::size_t i = offsets[v], end = offsets[v + 1]; i < end; ++i) {
        BrandesNode& w = st[adjacency[i]];
        if (w.dist < 0) {
          w.dist = next_dist;
          order[tail++] = adjacency[i];
        }
        if (w.dist == next_dist) {
          w.value += sigma_v;
        }
      }
    }

    // Backward, pull form: in reverse BFS order every node sums the coefficients (1 + δ_w)/σ_w
    // of its successors w (recognised by dist[w] == dist[v] + 1, no predecessor lists), so
    // δ_v = σ_v · Σ_w (1 + δ_w)/σ_w. Successors come later in BFS order and are already final.
    for (std::size_t k = tail; k-- > 1;) {
      const NodeId v = order[k];
      const std::int32_t next_dist = st[v].dist + 1;
      double sum = 0.0;
      for (std::size_t i = offsets[v], end = offsets[v + 1]; i < end; ++i) {
        const BrandesNode& w = st[adjacency[i]];
        if (w.dist == next_dist) {
          sum += w.value;
        }
      }
      const double sigma_v = st[v].value;
      const double delta_v = sigma_v * sum;
      acc[v] += delta_v;
      st[v].value = (1.0 + delta_v) / sigma_v;
    }

    // Reset only what this source touched.
    for (std::size_t k = 0; k < tail; ++k) {
      st[order[k]] = BrandesNode{0.0, -1};
    }
  }

 private:
  std::vector<BrandesNode> state_;
  std::vector<NodeId> order_;
};

/// k distinct pivots of [0, n) by a partial Fisher–Yates shuffle, returned sorted.
std::vector<NodeId> sample_pivots(std::size_t n, std::size_t k, std::uint64_t seed) {
  std::vector<NodeId> permutation(n);
  std::iota(permutation.begin(), permutation.end(), NodeId{0});
  SplitMix64 rng(seed);
  for (std::size_t i = 0; i < k; ++i) {
    const auto j = i + static_cast<std::size_t>(rng.bounded(n - i));
    std::swap(permutation[i], permutation[j]);
  }
  permutation.resize(k);
  std::sort(permutation.begin(), permutation.end());
  return permutation;
}

// ---------------------------------------------------------------------------------------------
// Closeness: bit-parallel multi-source BFS
// ---------------------------------------------------------------------------------------------

constexpr std::size_t kLanes = 64;

/// Adds one bit per lane of `x` to a bit-sliced counter (plane p holds bit p of every lane's
/// count). Amortised O(1) word operations.
inline void bit_sliced_add(std::uint64_t* planes, std::uint64_t x) noexcept {
  for (std::size_t p = 0; x != 0; ++p) {
    const std::uint64_t carry = planes[p] & x;
    planes[p] ^= x;
    x = carry;
  }
}

/// Scratch of one closeness worker; handles batches of up to 64 consecutive sources.
class ClosenessWorker {
 public:
  explicit ClosenessWorker(std::size_t n) : seen_(n), frontier_(n), next_(n) {}

  /// Writes closeness of sources [first, first + lanes) into out.
  void run_batch(const std::size_t* offsets, const NodeId* adjacency, std::size_t n,
                 std::size_t first, std::size_t lanes, double* out) {
    std::fill(seen_.begin(), seen_.end(), 0);
    std::fill(frontier_.begin(), frontier_.end(), 0);
    for (std::size_t j = 0; j < lanes; ++j) {
      seen_[first + j] = std::uint64_t{1} << j;
      frontier_[first + j] = std::uint64_t{1} << j;
    }
    std::array<std::uint64_t, kLanes> reached{};
    std::array<std::uint64_t, kLanes> distance_sum{};
    std::array<std::uint64_t, kLanes> planes{};
    const auto plane_count = static_cast<std::size_t>(std::bit_width(n));

    std::uint64_t* seen = seen_.data();
    std::uint64_t* frontier = frontier_.data();
    std::uint64_t* next = next_.data();
    for (std::uint64_t level = 1;; ++level) {
      // Push every frontier lane-set to the neighbours.
      std::fill(next, next + n, 0);
      for (std::size_t v = 0; v < n; ++v) {
        const std::uint64_t f = frontier[v];
        if (f == 0) {
          continue;
        }
        for (std::size_t i = offsets[v], end = offsets[v + 1]; i < end; ++i) {
          next[adjacency[i]] |= f;
        }
      }
      // Keep only first visits and count them per lane.
      std::fill(planes.begin(), planes.begin() + static_cast<std::ptrdiff_t>(plane_count), 0);
      bool any = false;
      for (std::size_t w = 0; w < n; ++w) {
        const std::uint64_t fresh = next[w] & ~seen[w];
        next[w] = fresh;
        if (fresh != 0) {
          seen[w] |= fresh;
          bit_sliced_add(planes.data(), fresh);
          any = true;
        }
      }
      if (!any) {
        break;
      }
      for (std::size_t j = 0; j < lanes; ++j) {
        std::uint64_t count = 0;
        for (std::size_t p = 0; p < plane_count; ++p) {
          count |= ((planes[p] >> j) & 1U) << p;
        }
        reached[j] += count;
        distance_sum[j] += count * level;
      }
      std::swap(frontier, next);
    }

    const double others = static_cast<double>(n - 1);
    for (std::size_t j = 0; j < lanes; ++j) {
      if (reached[j] == 0) {
        out[first + j] = 0.0;
        continue;
      }
      const auto r = static_cast<double>(reached[j]);
      const auto d = static_cast<double>(distance_sum[j]);
      out[first + j] = (r / d) * (r / others);
    }
  }

 private:
  std::vector<std::uint64_t> seen_;
  std::vector<std::uint64_t> frontier_;
  std::vector<std::uint64_t> next_;
};

}  // namespace

// -----------------------------------------------------------------------------------------------

PageRankResult pagerank(const CsrGraph& g, const PageRankOptions& opts) {
  if (!(opts.damping >= 0.0 && opts.damping <= 1.0)) {
    throw std::invalid_argument("pagerank: damping must be in [0, 1]");
  }
  if (!(opts.tolerance >= 0.0)) {
    throw std::invalid_argument("pagerank: tolerance must be >= 0");
  }
  if (opts.max_iterations < 1) {
    throw std::invalid_argument("pagerank: max_iterations must be >= 1");
  }

  PageRankResult result;
  const std::size_t n = g.node_count();
  if (n == 0) {
    result.converged = true;
    return result;
  }
  const std::size_t* offsets = g.offsets().data();
  const NodeId* adjacency = g.adjacency().data();
  const double nodes = static_cast<double>(n);
  const double d = opts.damping;

  std::vector<double> inverse_degree(n, 0.0);
  for (std::size_t u = 0; u < n; ++u) {
    const std::size_t degree = offsets[u + 1] - offsets[u];
    if (degree != 0) {
      inverse_degree[u] = 1.0 / static_cast<double>(degree);
    }
  }

  std::vector<double> x(n, 1.0 / nodes);
  std::vector<double> next(n, 0.0);
  std::vector<double> contribution(n, 0.0);
  for (int iteration = 1; iteration <= opts.max_iterations; ++iteration) {
    double dangling = 0.0;
    for (std::size_t u = 0; u < n; ++u) {
      if (inverse_degree[u] == 0.0) {
        dangling += x[u];
      }
      contribution[u] = x[u] * inverse_degree[u];
    }
    const double base = (1.0 - d) / nodes + d * dangling / nodes;
    double change = 0.0;
    for (std::size_t v = 0; v < n; ++v) {
      double sum = 0.0;
      for (std::size_t i = offsets[v], end = offsets[v + 1]; i < end; ++i) {
        sum += contribution[adjacency[i]];
      }
      next[v] = base + d * sum;
      change += std::abs(next[v] - x[v]);
    }
    x.swap(next);
    result.iterations = iteration;
    if (change <= opts.tolerance) {
      result.converged = true;
      break;
    }
  }

  const double total = std::accumulate(x.begin(), x.end(), 0.0);
  if (total > 0.0) {
    for (double& value : x) {
      value /= total;
    }
  }
  result.scores = std::move(x);
  return result;
}

BetweennessResult betweenness(const CsrGraph& g, const BetweennessOptions& opts) {
  BetweennessResult result;
  const std::size_t n = g.node_count();
  result.scores.assign(n, 0.0);
  if (n == 0) {
    return result;
  }

  std::vector<NodeId> pivots;  // empty: every node is a source
  std::size_t source_count = n;
  if (opts.sample_sources > 0 && opts.sample_sources < n) {
    pivots = sample_pivots(n, opts.sample_sources, opts.seed);
    source_count = pivots.size();
    result.approximate = true;
  }
  result.sources_used = source_count;

  // Fixed decomposition into blocks; depends only on the number of sources (not on threads).
  const std::size_t block = std::clamp<std::size_t>((source_count + 255) / 256, 1, 32);
  const std::size_t block_count = (source_count + block - 1) / block;
  const double work = static_cast<double>(source_count) *
                      static_cast<double>(n + g.adjacency().size());
  const unsigned threads = choose_threads(opts.threads, work, block_count);

  // Blocks are processed in rounds of `window`; each in-flight block owns one buffer of n doubles.
  constexpr std::size_t kBufferBudgetBytes = std::size_t{256} << 20U;
  const std::size_t affordable = std::max<std::size_t>(threads, kBufferBudgetBytes / (n * sizeof(double)));
  const std::size_t window =
      threads == 1 ? 1 : std::min({block_count, std::size_t{8} * threads, affordable});

  std::vector<double> buffers(window * n);
  std::vector<BrandesWorker> workers;
  workers.reserve(threads);
  for (unsigned t = 0; t < threads; ++t) {
    workers.emplace_back(n);
  }

  const std::size_t* offsets = g.offsets().data();
  const NodeId* adjacency = g.adjacency().data();
  std::vector<double>& total = result.scores;
  constexpr std::size_t kMergeChunk = 8192;
  const std::size_t merge_chunks = (n + kMergeChunk - 1) / kMergeChunk;

  for (std::size_t first_block = 0; first_block < block_count; first_block += window) {
    const std::size_t in_flight = std::min(window, block_count - first_block);
    parallel_for(0, in_flight, threads, [&](std::size_t slot, unsigned worker) {
      double* acc = buffers.data() + slot * n;
      std::fill(acc, acc + n, 0.0);
      const std::size_t lo = (first_block + slot) * block;
      const std::size_t hi = std::min(source_count, lo + block);
      for (std::size_t k = lo; k < hi; ++k) {
        const NodeId s = pivots.empty() ? static_cast<NodeId>(k) : pivots[k];
        workers[worker].accumulate(offsets, adjacency, s, acc);
      }
    });
    // Ordered reduction: for every node, buffers are added in block order.
    parallel_for(0, merge_chunks, threads, [&](std::size_t chunk) {
      const std::size_t lo = chunk * kMergeChunk;
      const std::size_t hi = std::min(n, lo + kMergeChunk);
      for (std::size_t slot = 0; slot < in_flight; ++slot) {
        const double* acc = buffers.data() + slot * n;
        for (std::size_t v = lo; v < hi; ++v) {
          total[v] += acc[v];
        }
      }
    });
  }

  // Undirected: every pair was counted from both endpoints.
  double scale = 0.5;
  if (result.approximate) {
    scale *= static_cast<double>(n) / static_cast<double>(source_count);
  }
  if (opts.normalized) {
    const auto nodes = static_cast<double>(n);
    scale = n > 2 ? scale * 2.0 / ((nodes - 1.0) * (nodes - 2.0)) : 0.0;
  }
  for (double& value : total) {
    value *= scale;
  }
  return result;
}

std::vector<double> closeness(const CsrGraph& g, unsigned threads) {
  const std::size_t n = g.node_count();
  std::vector<double> result(n, 0.0);
  if (n <= 1) {
    return result;
  }
  const std::size_t batches = (n + kLanes - 1) / kLanes;
  // Rough cost model: ~8 BFS levels per batch, each touching every node and edge.
  const double work =
      static_cast<double>(batches) * 8.0 * static_cast<double>(n + g.adjacency().size());
  const unsigned workers_wanted = choose_threads(threads, work, batches);

  std::vector<ClosenessWorker> workers;
  workers.reserve(workers_wanted);
  for (unsigned t = 0; t < workers_wanted; ++t) {
    workers.emplace_back(n);
  }
  const std::size_t* offsets = g.offsets().data();
  const NodeId* adjacency = g.adjacency().data();
  double* out = result.data();
  parallel_for(0, batches, workers_wanted, [&](std::size_t batch, unsigned worker) {
    const std::size_t first = batch * kLanes;
    const std::size_t lanes = std::min(kLanes, n - first);
    workers[worker].run_batch(offsets, adjacency, n, first, lanes, out);
  });
  return result;
}

std::vector<double> degree_centrality(const CsrGraph& g) {
  const std::size_t n = g.node_count();
  std::vector<double> result(n, 0.0);
  if (n <= 1) {
    return result;
  }
  const auto others = static_cast<double>(n - 1);
  const auto offsets = g.offsets();
  for (std::size_t u = 0; u < n; ++u) {
    result[u] = static_cast<double>(offsets[u + 1] - offsets[u]) / others;
  }
  return result;
}

}  // namespace sg::graph
