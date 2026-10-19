#include "sg/graph/generator.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace sg::graph {

namespace {

/// High and low halves of the 128-bit product a·b, in portable 64-bit arithmetic.
void multiply_64x64(std::uint64_t a, std::uint64_t b, std::uint64_t& high, std::uint64_t& low) noexcept {
  constexpr std::uint64_t kMask = 0xFFFFFFFFULL;
  const std::uint64_t a_lo = a & kMask;
  const std::uint64_t a_hi = a >> 32U;
  const std::uint64_t b_lo = b & kMask;
  const std::uint64_t b_hi = b >> 32U;
  const std::uint64_t lo_lo = a_lo * b_lo;
  const std::uint64_t lo_hi = a_lo * b_hi;
  const std::uint64_t hi_lo = a_hi * b_lo;
  const std::uint64_t hi_hi = a_hi * b_hi;
  const std::uint64_t middle = (lo_lo >> 32U) + (lo_hi & kMask) + (hi_lo & kMask);
  low = (middle << 32U) | (lo_lo & kMask);
  high = hi_hi + (lo_hi >> 32U) + (hi_lo >> 32U) + (middle >> 32U);
}

/// Largest accepted average degree (keeps the draw count and the output size sane).
constexpr double kMaxAverageDegree = 1.0e6;

}  // namespace

std::uint64_t SplitMix64::bounded(std::uint64_t n) {
  if (n == 0) {
    throw std::invalid_argument("SplitMix64::bounded: n must be > 0");
  }
  std::uint64_t high = 0;
  std::uint64_t low = 0;
  multiply_64x64(next(), n, high, low);
  if (low < n) {
    const std::uint64_t threshold = (std::uint64_t{0} - n) % n;  // 2^64 mod n
    while (low < threshold) {
      multiply_64x64(next(), n, high, low);
    }
  }
  return high;
}

std::vector<std::pair<NodeId, NodeId>> generate_homophilous_edges(
    std::span<const std::uint32_t> group_of_node, const GeneratorOptions& opts) {
  if (!std::isfinite(opts.average_degree) || opts.average_degree < 0.0 ||
      opts.average_degree > kMaxAverageDegree) {
    throw std::invalid_argument("generate_homophilous_edges: average_degree must be in [0, 1e6]");
  }
  if (!(opts.mixing >= 0.0 && opts.mixing <= 1.0)) {
    throw std::invalid_argument("generate_homophilous_edges: mixing must be in [0, 1]");
  }
  const std::size_t n = group_of_node.size();
  if (n > CsrGraph::kMaxNodes) {
    throw std::invalid_argument("generate_homophilous_edges: too many nodes");
  }
  const auto draws = static_cast<std::size_t>(std::llround(opts.average_degree / 2.0));

  // Members of every group in ascending node order; for each node its group's range and its
  // own position inside it.
  std::vector<NodeId> members(n);
  std::iota(members.begin(), members.end(), NodeId{0});
  std::stable_sort(members.begin(), members.end(), [&](NodeId a, NodeId b) {
    return group_of_node[a] < group_of_node[b];
  });
  std::vector<std::size_t> group_begin(n);
  std::vector<std::size_t> group_size(n);
  std::vector<std::size_t> position(n);
  for (std::size_t first = 0; first < n;) {
    std::size_t last = first;
    while (last < n && group_of_node[members[last]] == group_of_node[members[first]]) {
      ++last;
    }
    for (std::size_t i = first; i < last; ++i) {
      group_begin[members[i]] = first;
      group_size[members[i]] = last - first;
      position[members[i]] = i - first;
    }
    first = last;
  }

  SplitMix64 rng(opts.seed);
  const double p_inside = 1.0 - opts.mixing;
  std::vector<std::pair<NodeId, NodeId>> edges;
  edges.reserve(n * draws);
  for (std::size_t u = 0; u < n; ++u) {
    for (std::size_t t = 0; t < draws; ++t) {
      const double r = rng.uniform01();
      std::size_t v = 0;
      if (r < p_inside && group_size[u] > 1) {
        std::size_t j = static_cast<std::size_t>(rng.bounded(group_size[u] - 1));
        if (j >= position[u]) {
          ++j;  // skip the node itself
        }
        v = members[group_begin[u] + j];
      } else {
        if (n < 2) {
          continue;
        }
        v = static_cast<std::size_t>(rng.bounded(n - 1));
        if (v >= u) {
          ++v;  // skip the node itself
        }
      }
      if (v == u) {
        continue;  // unreachable by construction; kept as a guard
      }
      edges.emplace_back(static_cast<NodeId>(std::min(u, v)), static_cast<NodeId>(std::max(u, v)));
    }
  }
  std::sort(edges.begin(), edges.end());
  edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
  return edges;
}

}  // namespace sg::graph
