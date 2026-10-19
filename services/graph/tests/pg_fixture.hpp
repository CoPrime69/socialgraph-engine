#pragma once

// PostgreSQL fixture for graph-service integration tests: a throw-away database with the migrations
// applied and (optionally) the small fixture dataset of tests/fixtures imported.

#include <memory>
#include <string>

#include <gtest/gtest.h>

#include <sg/common/pg.hpp>
#include <sg/common/testing/test_env.hpp>

#include "sg/graph_service/infra/migrations.hpp"
#include "sg/graph_service/infra/postgres_graph_repository.hpp"
#include "sg/graph_service/infra/seed_loader.hpp"

namespace sg::graph_service::testing {

inline const char* fixtures_dir() { return SG_GRAPH_FIXTURES; }

class GraphDatabaseTest : public ::testing::Test {
 protected:
  /// Creates the database and applies the migrations; skips the test without SG_TEST_DATABASE_URL.
  void SetUp() override {
    const auto url = sg::common::testing::test_database_url();
    if (!url) {
      GTEST_SKIP() << "SG_TEST_DATABASE_URL not set";
    }
    db_ = std::make_unique<sg::common::testing::TempDatabase>(*url, "sg_graph");
    pool_ = make_pool(8);
    sg::common::run_migrations(*pool_, infra::embedded_migrations(), infra::kMigrationLockKey);
    repo_ = std::make_unique<infra::PostgresGraphRepository>(*pool_);
  }

  [[nodiscard]] std::unique_ptr<sg::common::PgPool> make_pool(std::size_t size) const {
    sg::common::PgPoolOptions o;
    o.conninfo = db_->url();
    o.size = size;
    o.application_name = "graph_tests";
    return std::make_unique<sg::common::PgPool>(o);
  }

  infra::SeedReport seed_fixtures() { return infra::seed_dataset_if_empty(*pool_, fixtures_dir()); }

  template <class T>
  T scalar(const std::string& sql) {
    return pool_->with_transaction([&](pqxx::work& tx) { return tx.query_value<T>(sql); });
  }

  void exec(const std::string& sql) {
    pool_->with_transaction([&](pqxx::work& tx) { tx.exec(sql); });
  }

  std::unique_ptr<sg::common::testing::TempDatabase> db_;
  std::unique_ptr<sg::common::PgPool> pool_;
  std::unique_ptr<infra::PostgresGraphRepository> repo_;
};

}  // namespace sg::graph_service::testing
