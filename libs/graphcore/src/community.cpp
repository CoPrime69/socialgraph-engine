#include "sg/graph/community.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace sg::graph {

namespace {

/// Weighted graph of one Louvain level. Self-loops are kept apart from the adjacency lists:
/// `loops[i]` is the weight of the loop at i, counted once in the total weight m and twice in
/// the strength of i.
struct LevelGraph {
  std::vector<std::size_t> offsets;
  std::vector<std::uint32_t> targets;
  std::vector<double> weights;
  std::vector<double> loops;

  [[nodiscard]] std::size_t size() const noexcept { return loops.size(); }
};

LevelGraph level_from_csr(const CsrGraph& g) {
  LevelGraph level;
  const auto offsets = g.offsets();
  const auto adjacency = g.adjacency();
  const auto weights = g.adjacency_weights();
  level.offsets.assign(offsets.begin(), offsets.end());
  level.targets.assign(adjacency.begin(), adjacency.end());
  level.weights.assign(weights.begin(), weights.end());
  level.loops.assign(g.node_count(), 0.0);
  return level;
}

/// k_i = Σ_j w_ij + 2·loop_i.
std::vector<double> strengths(const LevelGraph& level) {
  const std::size_t n = level.size();
  std::vector<double> k(n, 0.0);
  for (std::size_t i = 0; i < n; ++i) {
    double sum = 2.0 * level.loops[i];
    for (std::size_t e = level.offsets[i]; e < level.offsets[i + 1]; ++e) {
      sum += level.weights[e];
    }
    k[i] = sum;
  }
  return k;
}

/// Modularity of `community` (ids < level.size()) on a level graph with total strength m2 = 2m.
double level_modularity(const LevelGraph& level, const std::vector<std::uint32_t>& community,
                        const std::vector<double>& strength, double m2, double resolution,
                        std::vector<double>& inside, std::vector<double>& total) {
  const std::size_t n = level.size();
  std::fill(inside.begin(), inside.end(), 0.0);
  std::fill(total.begin(), total.end(), 0.0);
  for (std::size_t i = 0; i < n; ++i) {
    const std::uint32_t c = community[i];
    total[c] += strength[i];
    double in = 2.0 * level.loops[i];
    for (std::size_t e = level.offsets[i]; e < level.offsets[i + 1]; ++e) {
      if (community[level.targets[e]] == c) {
        in += level.weights[e];
      }
    }
    inside[c] += in;  // twice the internal weight
  }
  double q = 0.0;
  for (std::size_t c = 0; c < n; ++c) {
    if (total[c] == 0.0 && inside[c] == 0.0) {
      continue;
    }
    const double fraction = total[c] / m2;
    q += inside[c] / m2 - resolution * fraction * fraction;
  }
  return q;
}

struct LocalMovingOutcome {
  bool moved = false;
  double modularity = 0.0;
};

/// Louvain phase 1 on one level: repeatedly move single nodes (ascending id order) to the
/// neighbouring community with the best strictly positive gain.
LocalMovingOutcome local_moving(const LevelGraph& level, const std::vector<double>& strength,
                                double m2, const LouvainOptions& opts, double start_modularity,
                                std::vector<std::uint32_t>& community) {
  const std::size_t n = level.size();
  community.resize(n);
  std::iota(community.begin(), community.end(), std::uint32_t{0});
  std::vector<double> total(strength);  // Σ_tot of every community
  std::vector<double> link(n, 0.0);     // weight from the current node to each community
  std::vector<std::uint8_t> listed(n, 0);
  std::vector<std::uint32_t> candidates;
  std::vector<double> scratch_inside(n);
  std::vector<double> scratch_total(n);

  LocalMovingOutcome outcome;
  outcome.modularity = start_modularity;
  for (int pass = 0; pass < opts.max_passes; ++pass) {
    std::size_t moves = 0;
    for (std::size_t i = 0; i < n; ++i) {
      const std::uint32_t own = community[i];
      const double k_i = strength[i];
      for (std::size_t e = level.offsets[i]; e < level.offsets[i + 1]; ++e) {
        const std::uint32_t c = community[level.targets[e]];
        if (listed[c] == 0) {
          listed[c] = 1;
          link[c] = 0.0;
          candidates.push_back(c);
        }
        link[c] += level.weights[e];
      }
      total[own] -= k_i;  // take i out of its community
      const double factor = opts.resolution * k_i / m2;
      const double stay = (listed[own] != 0 ? link[own] : 0.0) - factor * total[own];
      std::uint32_t best = own;
      double best_gain = 0.0;
      for (const std::uint32_t c : candidates) {
        if (c == own) {
          continue;
        }
        const double gain = (link[c] - factor * total[c]) - stay;
        if (gain > best_gain) {
          best_gain = gain;
          best = c;
        }
      }
      total[best] += k_i;
      community[i] = best;
      if (best != own) {
        ++moves;
      }
      for (const std::uint32_t c : candidates) {
        listed[c] = 0;
      }
      candidates.clear();
    }
    if (moves == 0) {
      break;
    }
    outcome.moved = true;
    const double q =
        level_modularity(level, community, strength, m2, opts.resolution, scratch_inside, scratch_total);
    const double gain = q - outcome.modularity;
    outcome.modularity = q;
    if (gain < opts.min_gain) {
      break;
    }
  }
  return outcome;
}

/// Renumbers community ids densely in order of first appearance; returns the count.
std::size_t renumber_by_first_appearance(std::vector<std::uint32_t>& community) {
  std::vector<std::uint32_t> mapping(community.size(), kInvalidNode);
  std::uint32_t next = 0;
  for (std::uint32_t& c : community) {
    if (mapping[c] == kInvalidNode) {
      mapping[c] = next++;
    }
    c = mapping[c];
  }
  return next;
}

/// Louvain phase 2: one node per community, internal weight as a self-loop.
LevelGraph aggregate(const LevelGraph& level, const std::vector<std::uint32_t>& community,
                     std::size_t count) {
  const std::size_t n = level.size();
  std::vector<std::size_t> start(count + 1, 0);
  for (std::size_t i = 0; i < n; ++i) {
    ++start[community[i] + std::size_t{1}];
  }
  for (std::size_t c = 0; c < count; ++c) {
    start[c + 1] += start[c];
  }
  std::vector<std::uint32_t> members(n);
  {
    std::vector<std::size_t> cursor(start.begin(), start.end() - 1);
    for (std::size_t i = 0; i < n; ++i) {
      members[cursor[community[i]]++] = static_cast<std::uint32_t>(i);
    }
  }

  LevelGraph out;
  out.offsets.assign(count + 1, 0);
  out.loops.assign(count, 0.0);
  std::vector<double> link(count, 0.0);
  std::vector<std::uint8_t> listed(count, 0);
  std::vector<std::uint32_t> neighbours;
  for (std::size_t c = 0; c < count; ++c) {
    double loops = 0.0;
    double internal = 0.0;  // internal half-edges: every inner edge seen from both ends
    for (std::size_t m = start[c]; m < start[c + 1]; ++m) {
      const std::uint32_t i = members[m];
      loops += level.loops[i];
      for (std::size_t e = level.offsets[i]; e < level.offsets[i + 1]; ++e) {
        const std::uint32_t d = community[level.targets[e]];
        if (d == c) {
          internal += level.weights[e];
          continue;
        }
        if (listed[d] == 0) {
          listed[d] = 1;
          link[d] = 0.0;
          neighbours.push_back(d);
        }
        link[d] += level.weights[e];
      }
    }
    out.loops[c] = loops + internal / 2.0;
    std::sort(neighbours.begin(), neighbours.end());
    for (const std::uint32_t d : neighbours) {
      out.targets.push_back(d);
      out.weights.push_back(link[d]);
      listed[d] = 0;
    }
    neighbours.clear();
    out.offsets[c + 1] = out.targets.size();
  }
  return out;
}

/// Final ids: community size descending, then smallest member ascending.
std::size_t renumber_by_size(std::vector<std::uint32_t>& membership) {
  const std::size_t n = membership.size();
  std::size_t count = 0;
  for (const std::uint32_t c : membership) {
    count = std::max<std::size_t>(count, std::size_t{c} + 1);
  }
  std::vector<std::size_t> size(count, 0);
  std::vector<std::size_t> smallest(count, n);
  for (std::size_t i = 0; i < n; ++i) {
    const std::uint32_t c = membership[i];
    ++size[c];
    smallest[c] = std::min(smallest[c], i);
  }
  std::vector<std::uint32_t> order(count);
  std::iota(order.begin(), order.end(), std::uint32_t{0});
  order.erase(std::remove_if(order.begin(), order.end(), [&](std::uint32_t c) { return size[c] == 0; }),
              order.end());
  std::sort(order.begin(), order.end(), [&](std::uint32_t a, std::uint32_t b) {
    return size[a] > size[b] || (size[a] == size[b] && smallest[a] < smallest[b]);
  });
  std::vector<std::uint32_t> rank(count, kInvalidNode);
  for (std::size_t r = 0; r < order.size(); ++r) {
    rank[order[r]] = static_cast<std::uint32_t>(r);
  }
  for (std::uint32_t& c : membership) {
    c = rank[c];
  }
  return order.size();
}

void validate(const LouvainOptions& opts) {
  if (!(opts.resolution >= 0.0) || !std::isfinite(opts.resolution)) {
    throw std::invalid_argument("louvain: resolution must be finite and >= 0");
  }
  if (opts.max_levels < 1) {
    throw std::invalid_argument("louvain: max_levels must be >= 1");
  }
  if (opts.max_passes < 1) {
    throw std::invalid_argument("louvain: max_passes must be >= 1");
  }
  if (!(opts.min_gain >= 0.0)) {
    throw std::invalid_argument("louvain: min_gain must be >= 0");
  }
}

}  // namespace

CommunityResult louvain(const CsrGraph& g, const LouvainOptions& opts) {
  validate(opts);
  const auto all_weights = g.adjacency_weights();
  if (std::any_of(all_weights.begin(), all_weights.end(), [](double w) { return w < 0.0; })) {
    throw std::invalid_argument("louvain: edge weights must be >= 0");
  }

  CommunityResult result;
  const std::size_t n = g.node_count();
  result.membership.resize(n);
  std::iota(result.membership.begin(), result.membership.end(), std::uint32_t{0});
  if (n == 0) {
    return result;
  }
  const double m2 = 2.0 * g.total_weight();
  if (!(m2 > 0.0)) {
    result.community_count = n;  // singletons, already ordered by id
    return result;
  }

  LevelGraph level = level_from_csr(g);
  std::vector<double> k = strengths(level);
  // Modularity of the singleton partition (no self-loops at level 0).
  double current = 0.0;
  for (const double ki : k) {
    current -= opts.resolution * (ki / m2) * (ki / m2);
  }

  std::vector<std::uint32_t> community;
  for (int depth = 0; depth < opts.max_levels; ++depth) {
    const LocalMovingOutcome outcome = local_moving(level, k, m2, opts, current, community);
    if (!outcome.moved) {
      break;
    }
    if (depth > 0 && outcome.modularity - current < opts.min_gain) {
      break;  // negligible improvement: keep the coarser level we already have
    }
    const std::size_t count = renumber_by_first_appearance(community);
    for (std::uint32_t& c : result.membership) {
      c = community[c];
    }
    ++result.levels;
    current = outcome.modularity;
    if (count == level.size()) {
      break;
    }
    level = aggregate(level, community, count);
    k = strengths(level);
  }

  result.community_count = renumber_by_size(result.membership);
  result.modularity = modularity(g, result.membership, opts.resolution);
  return result;
}

double modularity(const CsrGraph& g, std::span<const std::uint32_t> membership,
                  double resolution) {
  const std::size_t n = g.node_count();
  if (membership.size() != n) {
    throw std::invalid_argument("modularity: membership size must equal node_count");
  }
  const double m = g.total_weight();
  if (n == 0 || m == 0.0) {
    return 0.0;
  }

  // Map labels to dense indices: directly when they are small, otherwise by sorting.
  std::vector<std::uint32_t> dense(membership.begin(), membership.end());
  const std::uint32_t max_label = *std::max_element(dense.begin(), dense.end());
  std::size_t count = std::size_t{max_label} + 1;
  if (count > 4 * n + 64) {
    std::vector<std::uint32_t> labels(dense);
    std::sort(labels.begin(), labels.end());
    labels.erase(std::unique(labels.begin(), labels.end()), labels.end());
    for (std::uint32_t& c : dense) {
      c = static_cast<std::uint32_t>(std::lower_bound(labels.begin(), labels.end(), c) - labels.begin());
    }
    count = labels.size();
  }

  std::vector<double> inside(count, 0.0);  // twice the internal weight
  std::vector<double> total(count, 0.0);   // sum of strengths
  const auto offsets = g.offsets();
  const auto adjacency = g.adjacency();
  const auto weights = g.adjacency_weights();
  for (std::size_t u = 0; u < n; ++u) {
    const std::uint32_t c = dense[u];
    for (std::size_t e = offsets[u]; e < offsets[u + 1]; ++e) {
      total[c] += weights[e];
      if (dense[adjacency[e]] == c) {
        inside[c] += weights[e];
      }
    }
  }
  const double m2 = 2.0 * m;
  double q = 0.0;
  for (std::size_t c = 0; c < count; ++c) {
    const double fraction = total[c] / m2;
    q += inside[c] / m2 - resolution * fraction * fraction;
  }
  return q;
}

}  // namespace sg::graph
