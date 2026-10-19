#pragma once

/// @file seed_loader.hpp
/// Imports the Stack Overflow 2008 dataset (contracts §10.1-§10.3) into an empty graph database:
/// categories, branches, users (profiles) and connections, plus the demo member profile connected to
/// the 20 highest-reputation imported users whose branch is `javascript` (contracts §10.2).

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string_view>

#include <sg/common/pg.hpp>

namespace sg::graph_service::infra {

/// The seeded demo member (identity-service seeds the matching account).
inline constexpr std::int64_t kDemoUserId = 100'000'000;
inline constexpr const char* kDemoHandle = "demo";
inline constexpr const char* kDemoName = "Demo Member";
inline constexpr const char* kDemoCategory = "Web Development";
inline constexpr const char* kDemoBranch = "javascript";
inline constexpr std::size_t kDemoConnections = 20;

struct SeedReport {
  bool loaded = false;  ///< false when the database already held a graph
  std::size_t categories = 0;
  std::size_t branches = 0;
  std::size_t users = 0;
  std::size_t connections = 0;
  std::size_t demo_connections = 0;
  std::int64_t graph_version = 0;
  double seconds = 0.0;
};

/// `<dir>/<name>.csv.gz`, or `<dir>/<name>.csv` when only the uncompressed file exists.
/// @throws std::runtime_error when neither exists.
[[nodiscard]] std::filesystem::path dataset_file(const std::filesystem::path& dir, std::string_view name);

/// Loads the dataset in one transaction (COPY via pqxx::stream_to) when the graph is empty (no
/// categories yet), sets graph_version to 1 and emits no events: consumers bootstrap from the
/// snapshot. Every row is validated first; errors name the file and line and nothing is committed.
/// Safe to run concurrently from several instances (advisory lock).
/// @throws std::runtime_error (sg::common::CsvError for row errors) on missing or invalid files.
SeedReport seed_dataset_if_empty(sg::common::PgPool& pool, const std::filesystem::path& dir);

}  // namespace sg::graph_service::infra
