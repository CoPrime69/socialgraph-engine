#include "sg/graph/csr_graph.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace sg::graph {

namespace {

/// One directed half-edge while building: neighbour and the index of the input edge it came from.
struct HalfEdge {
  NodeId neighbor;
  std::size_t input_index;
};

constexpr auto kHalfEdgeLess = [](const HalfEdge& a, const HalfEdge& b) noexcept {
  return a.neighbor < b.neighbor || (a.neighbor == b.neighbor && a.input_index < b.input_index);
};

}  // namespace

template <class EdgeAt>
CsrGraph CsrGraph::build(std::size_t node_count, std::size_t edge_input_count, EdgeAt edge_at) {
  if (node_count > kMaxNodes) {
    throw std::invalid_argument("CsrGraph: node_count exceeds CsrGraph::kMaxNodes");
  }

  // Pass 1: validate every edge and count half-edges per node (self-loops are dropped).
  std::vector<std::size_t> start(node_count + 1, 0);
  for (std::size_t i = 0; i < edge_input_count; ++i) {
    const WeightedEdge e = edge_at(i);
    if (e.u >= node_count || e.v >= node_count) {
      throw std::invalid_argument("CsrGraph: edge #" + std::to_string(i) + " (" +
                                  std::to_string(e.u) + ", " + std::to_string(e.v) +
                                  ") has an endpoint >= node_count " + std::to_string(node_count));
    }
    if (!std::isfinite(e.w)) {
      throw std::invalid_argument("CsrGraph: edge #" + std::to_string(i) +
                                  " has a non-finite weight");
    }
    if (e.u != e.v) {
      ++start[static_cast<std::size_t>(e.u) + 1];
      ++start[static_cast<std::size_t>(e.v) + 1];
    }
  }
  CsrGraph g;
  if (node_count == 0) {
    return g;
  }
  for (std::size_t u = 0; u < node_count; ++u) {
    start[u + 1] += start[u];
  }
  const std::size_t half_edges = start[node_count];

  // Pass 2: counting-sort the half-edges by source, keeping input order inside each list.
  std::vector<HalfEdge> scratch(half_edges);
  {
    std::vector<std::size_t> cursor(start.begin(), start.end() - 1);
    for (std::size_t i = 0; i < edge_input_count; ++i) {
      const WeightedEdge e = edge_at(i);
      if (e.u != e.v) {
        scratch[cursor[e.u]++] = HalfEdge{e.v, i};
        scratch[cursor[e.v]++] = HalfEdge{e.u, i};
      }
    }
  }

  // Pass 3: sort every list by (neighbour, input index) and keep the first of each neighbour,
  // i.e. the earliest input edge. Both directions of a pair keep the same input edge, so the
  // stored weights are symmetric.
  g.offsets_.assign(node_count + 1, 0);
  g.targets_.reserve(half_edges);
  g.weights_.reserve(half_edges);
  for (std::size_t u = 0; u < node_count; ++u) {
    const auto first = scratch.begin() + static_cast<std::ptrdiff_t>(start[u]);
    const auto last = scratch.begin() + static_cast<std::ptrdiff_t>(start[u + 1]);
    if (!std::is_sorted(first, last, kHalfEdgeLess)) {
      std::sort(first, last, kHalfEdgeLess);
    }
    NodeId previous = kInvalidNode;  // never a valid id, see kMaxNodes
    for (auto it = first; it != last; ++it) {
      if (it->neighbor == previous) {
        continue;
      }
      previous = it->neighbor;
      g.targets_.push_back(it->neighbor);
      g.weights_.push_back(edge_at(it->input_index).w);
    }
    g.offsets_[u + 1] = g.targets_.size();
    g.max_degree_ = std::max(g.max_degree_, g.offsets_[u + 1] - g.offsets_[u]);
  }
  if (g.targets_.size() != half_edges) {
    g.targets_.shrink_to_fit();
    g.weights_.shrink_to_fit();
  }

  double total = 0.0;
  for (std::size_t u = 0; u < node_count; ++u) {
    for (std::size_t i = g.offsets_[u]; i < g.offsets_[u + 1]; ++i) {
      if (u < g.targets_[i]) {
        total += g.weights_[i];
      }
    }
  }
  g.total_weight_ = total;
  return g;
}

CsrGraph CsrGraph::from_edges(std::size_t node_count, std::span<const WeightedEdge> edges) {
  return build(node_count, edges.size(), [edges](std::size_t i) { return edges[i]; });
}

CsrGraph CsrGraph::from_pairs(std::size_t node_count,
                              std::span<const std::pair<NodeId, NodeId>> edges) {
  return build(node_count, edges.size(), [edges](std::size_t i) {
    return WeightedEdge{edges[i].first, edges[i].second, 1.0};
  });
}

bool CsrGraph::has_edge(NodeId u, NodeId v) const {
  check_node(u);
  check_node(v);
  // Search the shorter of the two sorted lists.
  if (degree(u) > degree(v)) {
    std::swap(u, v);
  }
  const auto list = neighbors(u);
  return std::binary_search(list.begin(), list.end(), v);
}

std::optional<double> CsrGraph::edge_weight(NodeId u, NodeId v) const {
  check_node(u);
  check_node(v);
  const auto list = neighbors(u);
  const auto it = std::lower_bound(list.begin(), list.end(), v);
  if (it == list.end() || *it != v) {
    return std::nullopt;
  }
  return weights_[offsets_[u] + static_cast<std::size_t>(it - list.begin())];
}

std::vector<WeightedEdge> CsrGraph::edge_list() const {
  std::vector<WeightedEdge> out;
  out.reserve(edge_count());
  const std::size_t n = node_count();
  for (std::size_t u = 0; u < n; ++u) {
    for (std::size_t i = offsets_[u]; i < offsets_[u + 1]; ++i) {
      if (u < targets_[i]) {
        out.push_back(WeightedEdge{static_cast<NodeId>(u), targets_[i], weights_[i]});
      }
    }
  }
  return out;
}

}  // namespace sg::graph
