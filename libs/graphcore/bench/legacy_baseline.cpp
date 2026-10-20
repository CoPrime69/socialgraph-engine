// Faithful port of legacy/cpp files/connection_manager.cpp (see legacy_baseline.hpp). Statement
// order, containers and lookups follow the original; only `using namespace std`, the inert
// `#pragma omp` lines and the console output were dropped, and a source limit was added to the
// O(V·E) loops so the benchmark can time a prefix of the sources on large graphs.

#include "legacy_baseline.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <stack>

namespace sg::bench::legacy {

namespace {

class GraphTraversal {
 public:
  template <typename ProcessNode, typename ProcessEdge>
  static void bfs(const std::unordered_map<std::string, std::vector<std::string>>& graph, const std::string& start_id,
                  ProcessNode process_node, ProcessEdge process_edge) {
    std::unordered_map<std::string, bool> visited;
    std::queue<std::string> queue;

    visited[start_id] = true;
    queue.push(start_id);

    while (!queue.empty()) {
      std::string current = queue.front();
      queue.pop();

      process_node(current);

      if (graph.find(current) != graph.end()) {
        for (const auto& neighbor : graph.at(current)) {
          process_edge(current, neighbor);

          if (!visited[neighbor]) {
            visited[neighbor] = true;
            queue.push(neighbor);
          }
        }
      }
    }
  }

  static std::unordered_map<std::string, double> bfsDistance(
      const std::unordered_map<std::string, std::vector<std::string>>& graph, const std::string& start_id) {
    std::unordered_map<std::string, double> distances;

    bfs(
        graph, start_id,
        [&](const std::string& node) {
          if (distances.find(node) == distances.end()) {
            distances[node] = node == start_id ? 0.0 : std::numeric_limits<double>::max();
          }
        },
        [&](const std::string& from, const std::string& to) {
          double new_dist = distances[from] + 1;
          if (new_dist < distances[to]) {
            distances[to] = new_dist;
          }
        });

    return distances;
  }

  static std::vector<std::string> bfsPath(const std::unordered_map<std::string, std::vector<std::string>>& graph,
                                          const std::string& start_id, const std::string& end_id) {
    std::unordered_map<std::string, std::string> predecessors;
    bool found = false;

    bfs(
        graph, start_id,
        [&](const std::string& node) {
          if (node == end_id) {
            found = true;
          }
        },
        [&](const std::string& from, const std::string& to) {
          if (predecessors.find(to) == predecessors.end()) {
            predecessors[to] = from;
          }
        });

    if (!found) {
      return std::vector<std::string>();
    }

    std::vector<std::string> path;
    std::string current = end_id;
    while (current != start_id) {
      path.push_back(current);
      current = predecessors[current];
    }
    path.push_back(start_id);
    std::reverse(path.begin(), path.end());

    return path;
  }
};

}  // namespace

ConnectionManager::ConnectionManager(const BenchGraph& source) {
  const std::size_t n = source.graph.node_count();
  storage.reserve(n);
  users.reserve(n);
  for (std::size_t i = 0; i < n; ++i) {
    storage.push_back(std::make_unique<User>("User " + source.external_ids[i], source.external_ids[i], "-",
                                             source.categories[i], source.profiles[i].influence, source.branches[i]));
    users.push_back(storage.back().get());
  }
  // Same simple graph as the CSR: every edge in both lists, once (the legacy multigraph defect
  // is deliberately not reproduced so both sides process identical graphs).
  for (std::size_t i = 0; i < n; ++i) {
    auto& list = connections[users[i]->getID()];
    for (const graph::NodeId v : source.graph.neighbors(static_cast<graph::NodeId>(i))) {
      list.push_back(users[v]->getID());
    }
  }
}

std::unordered_map<std::string, double> ConnectionManager::calculatePageRank(int* iterations) {
  std::unordered_map<std::string, double> pagerank;
  std::unordered_map<std::string, double> out_degree;
  std::vector<std::string> dangling_nodes;  // Stores nodes with no outgoing edges

  // Initialize pagerank and calculate out-degrees
  for (const auto& user : users) {
    std::string user_id = user->getID();
    pagerank[user_id] = 1.0 / static_cast<double>(users.size());
    out_degree[user_id] = static_cast<double>(connections[user_id].size());

    if (out_degree[user_id] == 0) {
      dangling_nodes.push_back(user_id);  // Mark dangling nodes
    }
  }

  double damping_factor = 0.85;
  double tolerance = 1e-4;       // Start with a larger tolerance for faster initial iterations
  double min_tolerance = 1e-6;   // Minimum tolerance to tighten as convergence improves
  double tolerance_decay = 0.9;  // Factor to reduce tolerance per iteration
  bool converged = false;
  int rounds = 0;

  while (!converged) {
    ++rounds;
    std::unordered_map<std::string, double> new_pagerank;
    double total_pagerank = 0.0;
    double dangling_sum = 0.0;  // Contribution from dangling nodes

    // Sum contributions from dangling nodes
    for (const auto& dangling_id : dangling_nodes) {
      dangling_sum += pagerank[dangling_id];
    }
    dangling_sum *= damping_factor / static_cast<double>(users.size());

    for (size_t i = 0; i < users.size(); ++i) {
      User* user = users[i];
      std::string user_id = user->getID();
      double sum = 0.0;

      // Sum up the contributions from neighbors
      for (const auto& neighbor_id : connections[user_id]) {
        sum += pagerank[neighbor_id] / out_degree[neighbor_id];
      }

      // Calculate new PageRank for the current user, including dangling node contribution
      new_pagerank[user_id] =
          (1.0 - damping_factor) / static_cast<double>(users.size()) + damping_factor * sum + dangling_sum;
      total_pagerank += new_pagerank[user_id];
    }

    // Normalize new PageRank values and check for convergence
    converged = true;
    for (size_t i = 0; i < users.size(); ++i) {
      User* user = users[i];
      std::string user_id = user->getID();

      new_pagerank[user_id] /= total_pagerank;

      // Check convergence
      if (std::fabs(new_pagerank[user_id] - pagerank[user_id]) > tolerance) {
        converged = false;
      }
    }

    pagerank = std::move(new_pagerank);

    // Tighten the tolerance for the next iteration if not yet at min_tolerance
    tolerance = std::max(min_tolerance, tolerance * tolerance_decay);
  }

  if (iterations != nullptr) {
    *iterations = rounds;
  }
  return pagerank;
}

std::unordered_map<std::string, double> ConnectionManager::calculateBetweennessCentrality(std::size_t max_sources) {
  std::unordered_map<std::string, double> betweenness;

  {
    std::unordered_map<std::string, double> local_betweenness;

    for (size_t i = 0; i < std::min(users.size(), max_sources); i++) {
      const auto& user = users[i];
      std::queue<std::string> queue;
      std::unordered_map<std::string, double> distance;
      std::unordered_map<std::string, std::vector<std::string>> predecessors;
      std::stack<std::string> stack;
      std::unordered_map<std::string, double> dependency;

      for (const auto& u : users) {
        distance[u->getID()] = std::numeric_limits<double>::max();
      }

      distance[user->getID()] = 0;
      queue.push(user->getID());

      // Forward pass - shortest paths calculation
      while (!queue.empty()) {
        std::string curr = queue.front();
        queue.pop();
        stack.push(curr);

        for (const auto& neighbor_id : connections[curr]) {
          if (distance[neighbor_id] == std::numeric_limits<double>::max()) {
            queue.push(neighbor_id);
            distance[neighbor_id] = distance[curr] + 1;
          }
          if (distance[neighbor_id] == distance[curr] + 1) {
            predecessors[neighbor_id].push_back(curr);
          }
        }
      }

      // Backward pass - dependency accumulation
      while (!stack.empty()) {
        std::string w = stack.top();
        stack.pop();

        for (const auto& v : predecessors[w]) {
          double factor = (1.0 + dependency[w]) / static_cast<double>(predecessors[w].size());
          dependency[v] += factor;
        }

        if (w != user->getID()) {
          local_betweenness[w] += dependency[w];
        }
      }
    }

    // (was `#pragma omp critical`)
    for (const auto& pair : local_betweenness) {
      betweenness[pair.first] += pair.second;
    }
  }

  return betweenness;
}

std::unordered_map<std::string, double> ConnectionManager::calculateClosenessCentrality(std::size_t max_sources) {
  std::unordered_map<std::string, double> closeness;

  {
    std::unordered_map<std::string, double> local_closeness;

    for (size_t i = 0; i < std::min(users.size(), max_sources); i++) {
      const auto& user = users[i];
      auto distances = GraphTraversal::bfsDistance(connections, user->getID());

      double total_distance = 0.0;
      int reachable_nodes = 0;

      for (const auto& dist : distances) {
        if (dist.second != std::numeric_limits<double>::max()) {
          total_distance += dist.second;
          reachable_nodes++;
        }
      }

      // Normalize by number of reachable nodes
      if (reachable_nodes > 1) {  // Exclude self from count
        local_closeness[user->getID()] = (reachable_nodes - 1) / total_distance;
      } else {
        local_closeness[user->getID()] = 0.0;
      }
    }

    // (was `#pragma omp critical`)
    closeness.insert(local_closeness.begin(), local_closeness.end());
  }

  return closeness;
}

std::vector<std::string> ConnectionManager::bfsPath(const std::string& start_id, const std::string& end_id) {
  return GraphTraversal::bfsPath(connections, start_id, end_id);
}

}  // namespace sg::bench::legacy
