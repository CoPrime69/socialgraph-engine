#include "test_support.hpp"

#include "seed_fixture.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <numeric>
#include <sstream>
#include <stdexcept>

#ifndef SG_LEGACY_USERS_CSV
#define SG_LEGACY_USERS_CSV ""
#endif

namespace sg::graph::testing {

CsrGraph make_graph(std::size_t n, const Pairs& edges) { return CsrGraph::from_pairs(n, edges); }

CsrGraph path_graph(std::size_t n) {
  Pairs edges;
  for (std::size_t i = 0; i + 1 < n; ++i) {
    edges.emplace_back(static_cast<NodeId>(i), static_cast<NodeId>(i + 1));
  }
  return make_graph(n, edges);
}

CsrGraph cycle_graph(std::size_t n) {
  Pairs edges;
  for (std::size_t i = 0; i < n; ++i) {
    edges.emplace_back(static_cast<NodeId>(i), static_cast<NodeId>((i + 1) % n));
  }
  return make_graph(n, edges);
}

CsrGraph star_graph(std::size_t n) {
  Pairs edges;
  for (std::size_t i = 1; i < n; ++i) {
    edges.emplace_back(0, static_cast<NodeId>(i));
  }
  return make_graph(n, edges);
}

CsrGraph complete_graph(std::size_t n) {
  Pairs edges;
  for (std::size_t i = 0; i < n; ++i) {
    for (std::size_t j = i + 1; j < n; ++j) {
      edges.emplace_back(static_cast<NodeId>(i), static_cast<NodeId>(j));
    }
  }
  return make_graph(n, edges);
}

CsrGraph two_cliques_with_bridge(std::size_t s) {
  Pairs edges;
  for (std::size_t base : {std::size_t{0}, s}) {
    for (std::size_t i = 0; i < s; ++i) {
      for (std::size_t j = i + 1; j < s; ++j) {
        edges.emplace_back(static_cast<NodeId>(base + i), static_cast<NodeId>(base + j));
      }
    }
  }
  edges.emplace_back(static_cast<NodeId>(s - 1), static_cast<NodeId>(s));
  return make_graph(2 * s, edges);
}

CsrGraph ring_of_cliques(std::size_t cliques, std::size_t s) {
  Pairs edges;
  for (std::size_t c = 0; c < cliques; ++c) {
    const std::size_t base = c * s;
    for (std::size_t i = 0; i < s; ++i) {
      for (std::size_t j = i + 1; j < s; ++j) {
        edges.emplace_back(static_cast<NodeId>(base + i), static_cast<NodeId>(base + j));
      }
    }
    // last member of clique c — first member of clique c+1
    const std::size_t next_base = ((c + 1) % cliques) * s;
    edges.emplace_back(static_cast<NodeId>(base + s - 1), static_cast<NodeId>(next_base));
  }
  return make_graph(cliques * s, edges);
}

CsrGraph random_graph(std::size_t n, double p, std::uint64_t seed) {
  SplitMix64 rng(seed);
  Pairs edges;
  for (std::size_t i = 0; i < n; ++i) {
    for (std::size_t j = i + 1; j < n; ++j) {
      if (rng.uniform01() < p) {
        edges.emplace_back(static_cast<NodeId>(i), static_cast<NodeId>(j));
      }
    }
  }
  return make_graph(n, edges);
}

namespace {

std::vector<std::vector<double>> dense_weights(const CsrGraph& g) {
  const std::size_t n = g.node_count();
  std::vector<std::vector<double>> a(n, std::vector<double>(n, 0.0));
  for (const WeightedEdge& e : g.edge_list()) {
    a[e.u][e.v] = e.w;
    a[e.v][e.u] = e.w;
  }
  return a;
}

}  // namespace

std::vector<std::vector<int>> floyd_warshall(const CsrGraph& g) {
  const std::size_t n = g.node_count();
  constexpr int kInf = 1 << 28;
  std::vector<std::vector<int>> d(n, std::vector<int>(n, kInf));
  for (std::size_t i = 0; i < n; ++i) {
    d[i][i] = 0;
  }
  for (const WeightedEdge& e : g.edge_list()) {
    d[e.u][e.v] = 1;
    d[e.v][e.u] = 1;
  }
  for (std::size_t k = 0; k < n; ++k) {
    for (std::size_t i = 0; i < n; ++i) {
      for (std::size_t j = 0; j < n; ++j) {
        d[i][j] = std::min(d[i][j], d[i][k] + d[k][j]);
      }
    }
  }
  for (auto& row : d) {
    for (int& x : row) {
      if (x >= kInf) {
        x = kUnreachable;
      }
    }
  }
  return d;
}

std::vector<double> dense_pagerank(const CsrGraph& g, double damping, int iterations) {
  const std::size_t n = g.node_count();
  const auto nodes = static_cast<double>(n);
  const auto a = dense_weights(g);
  std::vector<double> degree(n, 0.0);
  for (std::size_t u = 0; u < n; ++u) {
    for (std::size_t v = 0; v < n; ++v) {
      degree[u] += a[u][v] != 0.0 ? 1.0 : 0.0;
    }
  }
  // Google matrix: column u is where the surfer at u goes next.
  std::vector<std::vector<double>> google(n, std::vector<double>(n, 0.0));
  for (std::size_t v = 0; v < n; ++v) {
    for (std::size_t u = 0; u < n; ++u) {
      double follow = 0.0;
      if (degree[u] == 0.0) {
        follow = 1.0 / nodes;
      } else if (a[u][v] != 0.0) {
        follow = 1.0 / degree[u];
      }
      google[v][u] = damping * follow + (1.0 - damping) / nodes;
    }
  }
  std::vector<double> x(n, 1.0 / nodes);
  std::vector<double> next(n);
  for (int it = 0; it < iterations; ++it) {
    for (std::size_t v = 0; v < n; ++v) {
      double sum = 0.0;
      for (std::size_t u = 0; u < n; ++u) {
        sum += google[v][u] * x[u];
      }
      next[v] = sum;
    }
    x.swap(next);
  }
  const double total = std::accumulate(x.begin(), x.end(), 0.0);
  for (double& value : x) {
    value /= total;
  }
  return x;
}

std::vector<double> brute_force_betweenness(const CsrGraph& g, bool normalized) {
  const std::size_t n = g.node_count();
  const auto d = floyd_warshall(g);
  const auto a = dense_weights(g);
  // σ[s][t] by dynamic programming over nodes sorted by distance from s.
  std::vector<std::vector<double>> sigma(n, std::vector<double>(n, 0.0));
  for (std::size_t s = 0; s < n; ++s) {
    std::vector<std::size_t> order;
    for (std::size_t t = 0; t < n; ++t) {
      if (d[s][t] != kUnreachable) {
        order.push_back(t);
      }
    }
    std::sort(order.begin(), order.end(), [&](std::size_t x, std::size_t y) { return d[s][x] < d[s][y]; });
    sigma[s][s] = 1.0;
    for (const std::size_t t : order) {
      if (t == s) {
        continue;
      }
      for (std::size_t u = 0; u < n; ++u) {
        if (a[u][t] != 0.0 && d[s][u] == d[s][t] - 1) {
          sigma[s][t] += sigma[s][u];
        }
      }
    }
  }
  std::vector<double> bc(n, 0.0);
  for (std::size_t s = 0; s < n; ++s) {
    for (std::size_t t = s + 1; t < n; ++t) {
      if (d[s][t] == kUnreachable) {
        continue;
      }
      for (std::size_t v = 0; v < n; ++v) {
        if (v == s || v == t || d[s][v] == kUnreachable || d[v][t] == kUnreachable) {
          continue;
        }
        if (d[s][v] + d[v][t] == d[s][t]) {
          bc[v] += sigma[s][v] * sigma[v][t] / sigma[s][t];
        }
      }
    }
  }
  if (normalized) {
    const auto nodes = static_cast<double>(n);
    for (double& x : bc) {
      x = n > 2 ? x / ((nodes - 1.0) * (nodes - 2.0) / 2.0) : 0.0;
    }
  }
  return bc;
}

std::vector<double> reference_closeness(const CsrGraph& g) {
  const std::size_t n = g.node_count();
  const auto d = floyd_warshall(g);
  std::vector<double> c(n, 0.0);
  for (std::size_t u = 0; u < n; ++u) {
    double reach = 0.0;
    double total = 0.0;
    for (std::size_t v = 0; v < n; ++v) {
      if (v != u && d[u][v] != kUnreachable) {
        reach += 1.0;
        total += d[u][v];
      }
    }
    c[u] = (reach == 0.0 || n <= 1) ? 0.0 : (reach / total) * (reach / static_cast<double>(n - 1));
  }
  return c;
}

double dense_modularity(const CsrGraph& g, const std::vector<std::uint32_t>& membership,
                        double resolution) {
  const std::size_t n = g.node_count();
  const auto a = dense_weights(g);
  std::vector<double> k(n, 0.0);
  double two_m = 0.0;
  for (std::size_t i = 0; i < n; ++i) {
    for (std::size_t j = 0; j < n; ++j) {
      k[i] += a[i][j];
    }
    two_m += k[i];
  }
  if (two_m == 0.0) {
    return 0.0;
  }
  double q = 0.0;
  for (std::size_t i = 0; i < n; ++i) {
    for (std::size_t j = 0; j < n; ++j) {
      if (membership[i] == membership[j]) {
        q += a[i][j] - resolution * k[i] * k[j] / two_m;
      }
    }
  }
  return q / two_m;
}

namespace {

std::vector<std::string> split_csv_line(const std::string& line) {
  std::vector<std::string> fields;
  std::string field;
  bool quoted = false;
  for (std::size_t i = 0; i < line.size(); ++i) {
    const char ch = line[i];
    if (quoted) {
      if (ch == '"' && i + 1 < line.size() && line[i + 1] == '"') {
        field += '"';
        ++i;
      } else if (ch == '"') {
        quoted = false;
      } else {
        field += ch;
      }
    } else if (ch == '"') {
      quoted = true;
    } else if (ch == ',') {
      fields.push_back(field);
      field.clear();
    } else if (ch != '\r') {
      field += ch;
    }
  }
  fields.push_back(field);
  return fields;
}

constexpr std::array<const char*, 5> kCategories = {"Engineering", "Finance", "HR", "Marketing", "Sales"};
constexpr std::array<const char*, 6> kBranches = {"software", "electrical", "accounting",
                                                  "human resources", "marketing", "sales"};

template <std::size_t N>
std::uint16_t code_of(const std::array<const char*, N>& names, const std::string& value) {
  for (std::size_t i = 0; i < N; ++i) {
    if (value == names[i]) {
      return static_cast<std::uint16_t>(i);
    }
  }
  throw std::runtime_error("seed data: unknown taxonomy value '" + value + "'");
}

}  // namespace

std::vector<SeedUser> seed_users() {
  std::vector<SeedUser> users;
  users.reserve(kSeedRows.size());
  for (const SeedRow& row : kSeedRows) {
    users.push_back(SeedUser{row.handle, row.category, row.branch, row.influence});
  }
  return users;
}

std::optional<std::vector<SeedUser>> load_legacy_seed_users() {
  std::ifstream in(SG_LEGACY_USERS_CSV);
  if (!in) {
    return std::nullopt;
  }
  std::string line;
  std::getline(in, line);  // header: name,id,password,category,influence,branch
  std::vector<SeedUser> users;
  while (users.size() < kSeedRows.size() && std::getline(in, line)) {
    if (line.empty() || line == "\r") {
      continue;
    }
    const auto fields = split_csv_line(line);
    if (fields.size() != 6) {
      throw std::runtime_error("legacy data: malformed line '" + line + "'");
    }
    users.push_back(SeedUser{fields[1], fields[3], fields[5], std::stod(fields[4])});
  }
  return users;
}

std::vector<Profile> seed_profiles(const std::vector<SeedUser>& users) {
  std::vector<Profile> profiles;
  profiles.reserve(users.size());
  for (const SeedUser& u : users) {
    profiles.push_back(Profile{code_of(kCategories, u.category), code_of(kBranches, u.branch), u.influence});
  }
  return profiles;
}

std::vector<std::uint32_t> seed_groups(const std::vector<SeedUser>& users) {
  std::vector<std::uint32_t> groups;
  groups.reserve(users.size());
  for (const Profile& p : seed_profiles(users)) {
    groups.push_back(static_cast<std::uint32_t>(p.category) * 6U + p.branch);
  }
  return groups;
}

}  // namespace sg::graph::testing
