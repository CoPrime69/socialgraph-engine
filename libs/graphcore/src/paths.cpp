#include "sg/graph/paths.hpp"

#include <algorithm>
#include <stdexcept>

namespace sg::graph {

namespace {

/// Per-thread scratch of the bidirectional search. A node is "seen" from one side when its stamp
/// equals the current epoch, so nothing has to be cleared between queries.
struct BidirectionalScratch {
  std::vector<std::uint32_t> stamp_forward;
  std::vector<std::uint32_t> stamp_backward;
  std::vector<NodeId> parent_forward;
  std::vector<NodeId> parent_backward;
  std::vector<NodeId> frontier_forward;
  std::vector<NodeId> frontier_backward;
  std::vector<NodeId> next;
  std::uint32_t epoch = 0;

  void prepare(std::size_t n) {
    if (stamp_forward.size() < n) {
      // New entries are 0, which is older than any live epoch.
      stamp_forward.resize(n, 0);
      stamp_backward.resize(n, 0);
      parent_forward.resize(n, kInvalidNode);
      parent_backward.resize(n, kInvalidNode);
    }
    if (++epoch == 0) {  // wrapped around: clear once every 2^32 queries
      std::fill(stamp_forward.begin(), stamp_forward.end(), 0);
      std::fill(stamp_backward.begin(), stamp_backward.end(), 0);
      epoch = 1;
    }
    frontier_forward.clear();
    frontier_backward.clear();
    next.clear();
  }
};

void check_node(const CsrGraph& g, NodeId u, const char* what) {
  if (static_cast<std::size_t>(u) >= g.node_count()) {
    throw std::out_of_range(what);
  }
}

}  // namespace

std::optional<std::vector<NodeId>> shortest_path(const CsrGraph& g, NodeId from, NodeId to) {
  check_node(g, from, "shortest_path: 'from' is out of range");
  check_node(g, to, "shortest_path: 'to' is out of range");
  if (from == to) {
    return std::vector<NodeId>{from};
  }

  thread_local BidirectionalScratch scratch;
  scratch.prepare(g.node_count());
  const std::uint32_t epoch = scratch.epoch;
  const std::size_t* offsets = g.offsets().data();
  const NodeId* adjacency = g.adjacency().data();

  scratch.stamp_forward[from] = epoch;
  scratch.parent_forward[from] = kInvalidNode;
  scratch.frontier_forward.push_back(from);
  scratch.stamp_backward[to] = epoch;
  scratch.parent_backward[to] = kInvalidNode;
  scratch.frontier_backward.push_back(to);

  // Invariant: the two visited sets are disjoint until they meet, so the first edge joining them
  // closes a shortest path (every node seen from a side is within that side's radius, and the
  // radii sum to less than the distance).
  NodeId meet_near = kInvalidNode;  // last node on the expanding side
  NodeId meet_far = kInvalidNode;   // first node already seen from the other side
  bool forward_side = true;
  while (!scratch.frontier_forward.empty() && !scratch.frontier_backward.empty()) {
    forward_side = scratch.frontier_forward.size() <= scratch.frontier_backward.size();
    auto& frontier = forward_side ? scratch.frontier_forward : scratch.frontier_backward;
    auto& own_stamp = forward_side ? scratch.stamp_forward : scratch.stamp_backward;
    auto& own_parent = forward_side ? scratch.parent_forward : scratch.parent_backward;
    const auto& other_stamp = forward_side ? scratch.stamp_backward : scratch.stamp_forward;
    scratch.next.clear();
    for (const NodeId v : frontier) {
      for (std::size_t i = offsets[v]; i < offsets[v + 1]; ++i) {
        const NodeId w = adjacency[i];
        if (other_stamp[w] == epoch) {
          meet_near = v;
          meet_far = w;
          break;
        }
        if (own_stamp[w] != epoch) {
          own_stamp[w] = epoch;
          own_parent[w] = v;
          scratch.next.push_back(w);
        }
      }
      if (meet_far != kInvalidNode) {
        break;
      }
    }
    if (meet_far != kInvalidNode) {
      break;
    }
    frontier.swap(scratch.next);
  }
  if (meet_far == kInvalidNode) {
    return std::nullopt;
  }

  // Assemble: from ... (forward parents) ... | ... (backward parents) ... to.
  const NodeId forward_end = forward_side ? meet_near : meet_far;
  const NodeId backward_start = forward_side ? meet_far : meet_near;
  std::vector<NodeId> path;
  for (NodeId v = forward_end; v != kInvalidNode; v = scratch.parent_forward[v]) {
    path.push_back(v);
  }
  std::reverse(path.begin(), path.end());
  for (NodeId v = backward_start; v != kInvalidNode; v = scratch.parent_backward[v]) {
    path.push_back(v);
  }
  return path;
}

std::vector<std::int32_t> bfs_distances(const CsrGraph& g, NodeId source) {
  check_node(g, source, "bfs_distances: 'source' is out of range");
  const std::size_t n = g.node_count();
  std::vector<std::int32_t> dist(n, -1);
  std::vector<NodeId> queue(n);
  const std::size_t* offsets = g.offsets().data();
  const NodeId* adjacency = g.adjacency().data();
  std::size_t head = 0;
  std::size_t tail = 0;
  dist[source] = 0;
  queue[tail++] = source;
  while (head < tail) {
    const NodeId v = queue[head++];
    const std::int32_t next = dist[v] + 1;
    for (std::size_t i = offsets[v]; i < offsets[v + 1]; ++i) {
      const NodeId w = adjacency[i];
      if (dist[w] < 0) {
        dist[w] = next;
        queue[tail++] = w;
      }
    }
  }
  return dist;
}

}  // namespace sg::graph
