#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "sg/common/errors.hpp"
#include "sg/common/pg.hpp"
#include "sg/common/testing/test_env.hpp"

namespace {

using namespace sg::common;
using namespace std::chrono_literals;
using sg::common::testing::TempDatabase;

class PgTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const auto url = sg::common::testing::test_database_url();
    if (!url) {
      GTEST_SKIP() << "SG_TEST_DATABASE_URL not set";
    }
    db_ = std::make_unique<TempDatabase>(*url, "sg_common_pg");
  }

  PgPoolOptions options(std::size_t size = 4) const {
    PgPoolOptions o;
    o.conninfo = db_->url();
    o.size = size;
    o.acquire_timeout = 300ms;
    o.application_name = "sg_common_tests";
    return o;
  }

  std::unique_ptr<TempDatabase> db_;
};

TEST_F(PgTest, TransactionCommitsAndRollsBack) {
  PgPool pool(options());
  pool.with_transaction([](pqxx::work& tx) { tx.exec("CREATE TABLE t (v integer NOT NULL)"); });
  const int inserted = pool.with_transaction([](pqxx::work& tx) {
    tx.exec_params("INSERT INTO t (v) VALUES ($1)", 1);
    return 1;
  });
  EXPECT_EQ(inserted, 1);
  EXPECT_THROW(pool.with_transaction([](pqxx::work& tx) {
    tx.exec_params("INSERT INTO t (v) VALUES ($1)", 2);
    throw std::runtime_error("abort");
  }),
               std::runtime_error);
  const auto count = pool.with_transaction([](pqxx::work& tx) { return tx.query_value<long>("SELECT count(*) FROM t"); });
  EXPECT_EQ(count, 1);
}

TEST_F(PgTest, ConnectionsAreConfigured) {
  PgPool pool(options());
  auto lease = pool.acquire();
  pqxx::nontransaction tx{*lease};
  EXPECT_EQ(tx.query_value<std::string>("SHOW statement_timeout"), "5s");
  EXPECT_EQ(tx.query_value<std::string>("SHOW application_name"), "sg_common_tests");
}

TEST_F(PgTest, StatementTimeoutAborts) {
  PgPoolOptions o = options();
  o.statement_timeout_ms = 100;
  PgPool pool(o);
  EXPECT_THROW(pool.with_transaction([](pqxx::work& tx) { tx.exec("SELECT pg_sleep(1)"); }), pqxx::sql_error);
  EXPECT_TRUE(pool.ping());  // the connection survives a cancelled statement
}

TEST_F(PgTest, ExhaustedPoolTimesOutWith503) {
  PgPool pool(options(2));
  auto a = pool.acquire();
  auto b = pool.acquire();
  try {
    auto c = pool.acquire();
    FAIL() << "acquired beyond the pool size";
  } catch (const ApiError& e) {
    EXPECT_EQ(e.status(), 503);
  }
  EXPECT_EQ(pool.stats().timeouts, 1U);
  EXPECT_EQ(pool.stats().in_use, 2U);
  {
    auto released = std::move(a);
  }
  EXPECT_NO_THROW({ auto c = pool.acquire(); });
  EXPECT_EQ(pool.stats().idle, 1U);
}

TEST_F(PgTest, BrokenConnectionsAreDiscardedAndReplaced) {
  PgPool pool(options(1));
  int backend_pid = 0;
  {
    auto lease = pool.acquire();
    backend_pid = lease->backendpid();
  }
  {
    // Kill the pooled connection from the server side.
    pqxx::connection admin{db_->url()};
    pqxx::nontransaction tx{admin};
    tx.exec_params("SELECT pg_terminate_backend($1)", backend_pid);
  }
  // The dead connection is detected at BEGIN and transparently replaced.
  const auto pid = pool.with_transaction([](pqxx::work& tx) { return tx.query_value<int>("SELECT pg_backend_pid()"); });
  EXPECT_NE(pid, backend_pid);
  EXPECT_TRUE(pool.ping());
}

TEST_F(PgTest, AutocommitReadsRetryOnABrokenConnection) {
  PgPool pool(options(1));
  int backend_pid = 0;
  {
    auto lease = pool.acquire();
    backend_pid = lease->backendpid();
  }
  {
    pqxx::connection admin{db_->url()};
    pqxx::nontransaction tx{admin};
    tx.exec_params("SELECT pg_terminate_backend($1)", backend_pid);
  }
  const auto pid = pool.with_read([](pqxx::nontransaction& tx) { return tx.query_value<int>("SELECT pg_backend_pid()"); });
  EXPECT_NE(pid, backend_pid);
  // No transaction is open around the statement.
  EXPECT_FALSE(pool.with_read([](pqxx::nontransaction& tx) {
    return tx.query_value<std::string>("SELECT current_setting('transaction_isolation')").empty();
  }));
  EXPECT_EQ(pool.with_read([](pqxx::nontransaction& tx) {
              return tx.query_value<long>("SELECT count(*) FROM pg_stat_activity WHERE pid = pg_backend_pid() "
                                          "AND backend_xid IS NULL AND xact_start = query_start");
            }),
            1);
}

TEST_F(PgTest, VacuumAfterImportKeepsTheSessionTimeout) {
  PgPool pool(options(1));
  pool.with_transaction([](pqxx::work& tx) { tx.exec("CREATE TABLE imported (id integer)"); });
  const auto before = pool.with_read(
      [](pqxx::nontransaction& tx) { return tx.query_value<std::string>("SELECT current_setting('statement_timeout')"); });
  vacuum_analyze(pool, "imported");
  vacuum_analyze(pool, "does_not_exist");  // logged, not thrown
  EXPECT_EQ(pool.with_read([](pqxx::nontransaction& tx) {
              return tx.query_value<std::string>("SELECT current_setting('statement_timeout')");
            }),
            before);
  EXPECT_TRUE(pool.with_read([](pqxx::nontransaction& tx) {
    return tx.query_value<bool>("SELECT last_vacuum IS NOT NULL FROM pg_stat_user_tables WHERE relname = 'imported'");
  }));
}

TEST_F(PgTest, UnreachableDatabaseIs503) {
  PgPoolOptions o;
  o.conninfo = "postgresql://nobody:nothing@127.0.0.1:1/none";
  o.connect_timeout_s = 1;
  PgPool pool(o);
  try {
    (void)pool.acquire();
    FAIL() << "connected to a closed port";
  } catch (const ApiError& e) {
    EXPECT_EQ(e.status(), 503);
    EXPECT_EQ(std::string(e.what()).find("nothing"), std::string::npos);  // no credentials in errors
  }
  EXPECT_FALSE(pool.ping());
  EXPECT_EQ(pool.stats().in_use, 0U);
}

TEST_F(PgTest, ConcurrentUseNeverExceedsPoolSize) {
  PgPool pool(options(3));
  std::atomic<int> failures{0};
  std::vector<std::thread> threads;
  for (int t = 0; t < 8; ++t) {
    threads.emplace_back([&] {
      for (int i = 0; i < 20; ++i) {
        try {
          pool.with_transaction([](pqxx::work& tx) { tx.exec("SELECT pg_sleep(0.001)"); });
        } catch (const std::exception&) {
          failures.fetch_add(1);
        }
      }
    });
  }
  for (auto& t : threads) {
    t.join();
  }
  const auto stats = pool.stats();
  EXPECT_LE(stats.idle, 3U);
  EXPECT_EQ(stats.in_use, 0U);
  EXPECT_EQ(stats.acquisitions, 160U - static_cast<std::uint64_t>(stats.timeouts));
  EXPECT_EQ(static_cast<std::uint64_t>(failures.load()), stats.timeouts);
}

const std::vector<Migration> kMigrations{
    {1, "create_a", "CREATE TABLE a (id integer PRIMARY KEY);"},
    {2, "create_b", "CREATE TABLE b (id integer PRIMARY KEY, a_id integer REFERENCES a(id));\nINSERT INTO a VALUES (1);"},
};

TEST_F(PgTest, MigrationsAreAppliedOnceAndRecorded) {
  PgPool pool(options());
  run_migrations(pool, kMigrations, 4242);
  run_migrations(pool, kMigrations, 4242);  // idempotent
  pool.with_transaction([](pqxx::work& tx) {
    EXPECT_EQ(tx.query_value<long>("SELECT count(*) FROM a"), 1);
    const auto rows = tx.exec("SELECT version, name FROM schema_migrations ORDER BY version");
    ASSERT_EQ(rows.size(), 2U);
    EXPECT_EQ(rows[0][1].as<std::string>(), "create_a");
    EXPECT_EQ(rows[1][1].as<std::string>(), "create_b");
  });
}

TEST_F(PgTest, MigrationsCanBeAddedLater) {
  PgPool pool(options());
  run_migrations(pool, std::span(kMigrations).first(1), 4242);
  run_migrations(pool, kMigrations, 4242);
  EXPECT_EQ(pool.with_transaction([](pqxx::work& tx) { return tx.query_value<long>("SELECT count(*) FROM schema_migrations"); }), 2);
}

TEST_F(PgTest, RenamedAppliedMigrationIsRejected) {
  PgPool pool(options());
  run_migrations(pool, kMigrations, 4242);
  std::vector<Migration> rewritten = kMigrations;
  rewritten[0].name = "something_else";
  EXPECT_THROW(run_migrations(pool, rewritten, 4242), std::runtime_error);
}

TEST_F(PgTest, FailingMigrationRollsBackCompletely) {
  PgPool pool(options());
  const std::vector<Migration> broken{
      {1, "create_a", "CREATE TABLE a (id integer PRIMARY KEY);"},
      {2, "broken", "CREATE TABLE c (id integer); SELECT * FROM does_not_exist;"},
  };
  EXPECT_THROW(run_migrations(pool, broken, 4242), std::exception);
  pool.with_transaction([](pqxx::work& tx) {
    EXPECT_EQ(tx.query_value<long>("SELECT count(*) FROM schema_migrations"), 1);
    EXPECT_FALSE(tx.query_value<bool>("SELECT to_regclass('c') IS NOT NULL"));
  });
}

TEST_F(PgTest, ConcurrentMigratorsApplyEachMigrationOnce) {
  std::vector<std::thread> threads;
  std::atomic<int> errors{0};
  for (int i = 0; i < 4; ++i) {
    threads.emplace_back([&] {
      try {
        PgPool pool(options(1));
        run_migrations(pool, kMigrations, 4242);
      } catch (const std::exception&) {
        errors.fetch_add(1);
      }
    });
  }
  for (auto& t : threads) {
    t.join();
  }
  EXPECT_EQ(errors.load(), 0);
  PgPool pool(options());
  EXPECT_EQ(pool.with_transaction([](pqxx::work& tx) { return tx.query_value<long>("SELECT count(*) FROM a"); }), 1);
}

TEST_F(PgTest, InvalidMigrationListsAreRejected) {
  PgPool pool(options());
  const std::vector<Migration> unordered{{2, "b", "SELECT 1"}, {1, "a", "SELECT 1"}};
  EXPECT_THROW(run_migrations(pool, unordered, 1), std::invalid_argument);
  const std::vector<Migration> unnamed{{1, "", "SELECT 1"}};
  EXPECT_THROW(run_migrations(pool, unnamed, 1), std::invalid_argument);
}

}  // namespace
