#include "sg/graph/recommend.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace sg::graph {

namespace {

struct Scored {
  double score;
  NodeId node;
};

/// Strict "ranks before": higher score first, then smaller id.
constexpr auto kRanksBefore = [](const Scored& a, const Scored& b) noexcept {
  return a.score > b.score || (a.score == b.score && a.node < b.node);
};

void validate(const RecommendOptions& opts) {
  for (const double w : {opts.w_structural, opts.w_affinity, opts.w_community}) {
    if (!(w >= 0.0) || !std::isfinite(w)) {
      throw std::invalid_argument("recommend: weights must be finite and >= 0");
    }
  }
}

/// Per-thread scratch: the Adamic-Adar sums and common-neighbour counts of one query. Every entry
/// is zero between queries; a query records the nodes it touches and resets only those, so the
/// structural part costs O(Σ deg z) instead of allocating and clearing O(V) memory per call.
struct RecommendScratch {
  std::vector<double> adamic_adar;
  std::vector<std::uint32_t> mutual;
  std::vector<NodeId> touched;
};

/// Borrows the thread's scratch for one query and restores the all-zero state when the query
/// ends, also when it ends with an exception.
class ScratchLease {
 public:
  ScratchLease(RecommendScratch& scratch, std::size_t n) : scratch_(scratch) {
    if (scratch_.adamic_adar.size() < n || scratch_.mutual.size() < n) {
      scratch_.adamic_adar.resize(n, 0.0);  // new entries are zero, like the others
      scratch_.mutual.resize(n, 0);
    }
    scratch_.touched.clear();
  }
  ScratchLease(const ScratchLease&) = delete;
  ScratchLease& operator=(const ScratchLease&) = delete;
  ~ScratchLease() {
    for (const NodeId v : scratch_.touched) {
      scratch_.adamic_adar[v] = 0.0;
      scratch_.mutual[v] = 0;
    }
    scratch_.touched.clear();
  }

 private:
  RecommendScratch& scratch_;
};

/// Calls fn(v) for every v != user that is not a neighbour of user, in ascending order.
template <class Fn>
void for_each_candidate(std::size_t n, NodeId user, std::span<const NodeId> user_neighbors, Fn&& fn) {
  auto next_neighbor = user_neighbors.begin();
  for (std::size_t v = 0; v < n; ++v) {
    while (next_neighbor != user_neighbors.end() && *next_neighbor < v) {
      ++next_neighbor;
    }
    if (v == user || (next_neighbor != user_neighbors.end() && *next_neighbor == v)) {
      continue;
    }
    fn(static_cast<NodeId>(v));
  }
}

}  // namespace

std::vector<Recommendation> recommend(const CsrGraph& g, std::span<const Profile> profiles,
                                      std::span<const std::uint32_t> membership, NodeId user,
                                      const RecommendOptions& opts) {
  const std::size_t n = g.node_count();
  if (profiles.size() != n) {
    throw std::invalid_argument("recommend: profiles.size() must equal node_count");
  }
  if (!membership.empty() && membership.size() != n) {
    throw std::invalid_argument("recommend: membership must be empty or of size node_count");
  }
  validate(opts);
  if (static_cast<std::size_t>(user) >= n) {
    throw std::out_of_range("recommend: user is out of range");
  }
  if (opts.limit == 0) {
    return {};
  }

  // Adamic-Adar over friends of friends: Σ_{z ∈ N(u) ∩ N(v)} 1 / ln deg(z).
  const std::size_t* offsets = g.offsets().data();
  const NodeId* adjacency = g.adjacency().data();
  const std::span<const NodeId> user_neighbors = g.neighbors(user);
  thread_local RecommendScratch scratch;
  const ScratchLease lease(scratch, n);
  double* adamic_adar = scratch.adamic_adar.data();
  std::uint32_t* mutual = scratch.mutual.data();
  for (const NodeId z : user_neighbors) {
    const std::size_t degree = offsets[z + 1] - offsets[z];
    if (degree < 2) {
      continue;  // z only knows the user: it is nobody else's common neighbour
    }
    const double weight = 1.0 / std::log(static_cast<double>(degree));
    for (std::size_t i = offsets[z]; i < offsets[z + 1]; ++i) {
      const NodeId v = adjacency[i];
      if (v == user) {
        continue;
      }
      if (mutual[v] == 0) {
        scratch.touched.push_back(v);  // may throw; v is not modified before it is recorded
      }
      adamic_adar[v] += weight;
      ++mutual[v];
    }
  }
  // Only touched nodes can have a positive index; the user's neighbours are not candidates.
  double max_adamic_adar = 0.0;
  for (const NodeId v : scratch.touched) {
    if (!std::binary_search(user_neighbors.begin(), user_neighbors.end(), v)) {
      max_adamic_adar = std::max(max_adamic_adar, adamic_adar[v]);
    }
  }

  const Profile& me = profiles[user];
  const bool with_communities = !membership.empty();
  auto structural = [&](NodeId v) {
    return max_adamic_adar > 0.0 ? adamic_adar[v] / max_adamic_adar : 0.0;
  };
  auto same_community = [&](NodeId v) {
    return with_communities && membership[v] == membership[user];
  };

  // Bounded heap whose front is the weakest of the current best `limit`.
  std::vector<Scored> best;
  best.reserve(std::min(opts.limit, n));
  for_each_candidate(n, user, user_neighbors, [&](NodeId v) {
    const double score = opts.w_structural * structural(v) +
                         opts.w_affinity * affinity(me, profiles[v]) +
                         opts.w_community * (same_community(v) ? 1.0 : 0.0);
    const Scored candidate{score, v};
    if (best.size() < opts.limit) {
      best.push_back(candidate);
      std::push_heap(best.begin(), best.end(), kRanksBefore);
    } else if (kRanksBefore(candidate, best.front())) {
      std::pop_heap(best.begin(), best.end(), kRanksBefore);
      best.back() = candidate;
      std::push_heap(best.begin(), best.end(), kRanksBefore);
    }
  });
  std::sort(best.begin(), best.end(), kRanksBefore);

  std::vector<Recommendation> out;
  out.reserve(best.size());
  for (const Scored& s : best) {
    const Profile& other = profiles[s.node];
    out.push_back(Recommendation{s.node, s.score, mutual[s.node], adamic_adar[s.node],
                                 affinity(me, other), me.category == other.category,
                                 me.branch == other.branch, same_community(s.node)});
  }
  return out;
}

}  // namespace sg::graph
