#pragma once

// The "before" of the benchmarks: a faithful re-implementation of the legacy data structures and
// algorithms of legacy/cpp files/connection_manager.cpp: users as heap objects with string ids
// (getters return copies), the graph as unordered_map<string, vector<string>>, and
// calculatePageRank / calculateBetweennessCentrality / calculateClosenessCentrality /
// GraphTraversal::bfsPath with their original logic, defects included (closeness is +inf,
// betweenness splits dependencies by predecessor count). The legacy build never passed
// -fopenmp, so its `#pragma omp` lines were inert; the code here is the same single-threaded
// program without them. Only the benchmark harness uses this code.

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "dataset.hpp"

namespace sg::bench::legacy {

/// legacy/header files/user.h
class User {
 public:
  User(const std::string& name, const std::string& id, const std::string& password,
       const std::string& category, double influence, const std::string& branch)
      : name_(name), id_(id), password_(password), category_(category), influence_(influence), branch_(branch) {}

  std::string getName() const { return name_; }
  std::string getID() const { return id_; }
  std::string getPassword() const { return password_; }
  std::string getCategory() const { return category_; }
  double getInfluence() const { return influence_; }
  std::string getBranch() const { return branch_; }

 private:
  std::string name_;
  std::string id_;
  std::string password_;
  std::string category_;
  double influence_;
  std::string branch_;
};

/// The parts of legacy ConnectionManager that the benchmarks compare against.
class ConnectionManager {
 public:
  /// Builds `users` and the string-keyed `connections` (both directions, no duplicates: the
  /// same simple graph the new library sees).
  explicit ConnectionManager(const BenchGraph& source);

  /// Legacy calculatePageRank (decaying max-abs tolerance 1e-4 → 1e-6, renormalised each round).
  std::unordered_map<std::string, double> calculatePageRank(int* iterations = nullptr);
  /// Legacy calculateBetweennessCentrality over the first `max_sources` users (all by default).
  std::unordered_map<std::string, double> calculateBetweennessCentrality(std::size_t max_sources = static_cast<std::size_t>(-1));
  /// Legacy calculateClosenessCentrality over the first `max_sources` users (all by default).
  std::unordered_map<std::string, double> calculateClosenessCentrality(std::size_t max_sources = static_cast<std::size_t>(-1));
  /// Legacy GraphTraversal::bfsPath.
  std::vector<std::string> bfsPath(const std::string& start_id, const std::string& end_id);

  [[nodiscard]] std::size_t user_count() const { return users.size(); }

 private:
  std::unordered_map<std::string, std::vector<std::string>> connections;
  std::vector<User*> users;                    // the legacy code held raw owning pointers...
  std::vector<std::unique_ptr<User>> storage;  // ...owned here so the harness does not leak
};

}  // namespace sg::bench::legacy
