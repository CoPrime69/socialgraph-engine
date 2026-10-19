#pragma once

/// @file postgres_graph_repository.hpp
/// GraphRepository on PostgreSQL. All SQL is parameterized. Every write transaction locks the
/// graph_meta row (SELECT ... FOR UPDATE) so writers are serialised, then bumps graph_version once
/// per change and enqueues the event carrying that version (contracts §7).
///
/// Connection writes use group commit: callers hand their write to one writer thread, which
/// applies every write waiting at that moment in a single transaction (one lock, one WAL flush)
/// and then answers each caller. The serialised section used to cost one disk flush per write;
/// now it costs one per batch. Business outcomes (already connected, not connected) are detected
/// without errors, so one write never fails its batch-mates; a database error fails the batch.

#include <cstddef>
#include <memory>

#include <sg/common/pg.hpp>

#include "sg/graph_service/app/graph_repository.hpp"

namespace sg::graph_service::infra {

class PostgresGraphRepository final : public app::GraphRepository {
 public:
  struct Options {
    std::size_t max_batch = 512;  ///< connection writes per group-commit transaction
  };

  explicit PostgresGraphRepository(sg::common::PgPool& pool) : PostgresGraphRepository(pool, Options{}) {}
  PostgresGraphRepository(sg::common::PgPool& pool, Options options);
  ~PostgresGraphRepository() override;
  PostgresGraphRepository(const PostgresGraphRepository&) = delete;
  PostgresGraphRepository& operator=(const PostgresGraphRepository&) = delete;

  [[nodiscard]] domain::Taxonomy load_taxonomy() override;
  [[nodiscard]] std::optional<domain::ProfileWithCount> find_profile(std::int64_t user_id) override;
  [[nodiscard]] domain::UserPage list_profiles(const domain::UserQuery& query) override;
  [[nodiscard]] domain::ConnectionList list_connections(std::int64_t user_id) override;
  domain::AddedConnection add_connection(std::int64_t user_id, std::int64_t other_id, double strength) override;
  std::int64_t remove_connection(std::int64_t user_id, std::int64_t other_id) override;
  bool create_member_profile(const domain::NewMember& member, const std::string& stream,
                             const std::string& event_id) override;
  void record_dead_letter(const std::string& stream, const std::string& entry_id, const nlohmann::json& fields,
                          const std::string& error) override;
  [[nodiscard]] domain::Snapshot snapshot() override;

  /// Current graph_version.
  [[nodiscard]] std::int64_t graph_version();

 private:
  class GroupCommitWriter;

  sg::common::PgPool& pool_;
  std::unique_ptr<GroupCommitWriter> writer_;
};

/// Locks the graph_meta row for the rest of `tx` (serialises graph writers) and returns the version.
std::int64_t lock_graph_version(pqxx::work& tx);
/// Bumps graph_version by one inside `tx` (after lock_graph_version) and returns the new version.
std::int64_t bump_graph_version(pqxx::work& tx);

}  // namespace sg::graph_service::infra
