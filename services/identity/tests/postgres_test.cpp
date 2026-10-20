#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <sg/common/auth.hpp>
#include <sg/common/crypto.hpp>
#include <sg/common/errors.hpp>
#include <sg/common/events.hpp>
#include <sg/common/outbox.hpp>
#include <sg/common/pg.hpp>
#include <sg/common/testing/test_env.hpp>

#include "fakes.hpp"
#include "sg/identity/app/account_service.hpp"
#include "sg/identity/infra/demo_seed.hpp"
#include "sg/identity/infra/migrations.hpp"
#include "sg/identity/infra/postgres_account_repository.hpp"

namespace {

using namespace std::chrono_literals;
using nlohmann::json;
using sg::common::ApiError;
using sg::common::PasswordHashParams;
using sg::common::PgPool;
using sg::common::testing::TempDatabase;
using sg::identity::domain::Registration;
using sg::identity::infra::PostgresAccountRepository;

Registration registration(const std::string& handle) {
  Registration r;
  r.handle = handle;
  r.name = "Name of " + handle;
  r.password = "correct horse";
  r.category = "Web Development";
  r.branch = "javascript";
  r.about = "about " + handle;
  return r;
}

class IdentityPostgresTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const auto url = sg::common::testing::test_database_url();
    if (!url) {
      GTEST_SKIP() << "SG_TEST_DATABASE_URL not set";
    }
    db_ = std::make_unique<TempDatabase>(*url, "sg_identity");
    sg::common::PgPoolOptions o;
    o.conninfo = db_->url();
    o.size = 6;
    o.application_name = "identity_tests";
    pool_ = std::make_unique<PgPool>(o);
    sg::common::run_migrations(*pool_, sg::identity::infra::embedded_migrations(), sg::identity::infra::kMigrationLockKey);
    repo_ = std::make_unique<PostgresAccountRepository>(*pool_);
  }

  template <class T>
  T scalar(const std::string& sql) {
    return pool_->with_transaction([&](pqxx::work& tx) { return tx.query_value<T>(sql); });
  }

  std::string hash(const std::string& password = "correct horse") const {
    return sg::common::hash_password(password, PasswordHashParams::fast_for_tests());
  }

  std::unique_ptr<TempDatabase> db_;
  std::unique_ptr<PgPool> pool_;
  std::unique_ptr<PostgresAccountRepository> repo_;
};

TEST_F(IdentityPostgresTest, CreateStoresAccountAndOutboxEventAtomically) {
  const auto account = repo_->create(registration("ada_l"), hash());
  EXPECT_EQ(account.id, 100000001);  // 100000000 is reserved for the demo account
  EXPECT_EQ(account.handle, "ada_l");
  EXPECT_GT(account.created_at, 0);
  EXPECT_FALSE(account.last_login_at.has_value());

  const auto rows = pool_->with_transaction([](pqxx::work& tx) {
    return tx.exec("SELECT type, payload::text, published_at IS NULL FROM outbox ORDER BY id");
  });
  ASSERT_EQ(rows.size(), 1U);
  EXPECT_EQ(rows[0][0].as<std::string>(), "user.registered");
  EXPECT_EQ(json::parse(rows[0][1].as<std::string>()),
            json::parse(R"({"user_id":100000001,"handle":"ada_l","name":"Name of ada_l","category":"Web Development",
                           "branch":"javascript","location":null,"about":"about ada_l"})"));
  EXPECT_TRUE(rows[0][2].as<bool>());

  const auto found = repo_->find_by_handle("ada_l");
  ASSERT_TRUE(found);
  EXPECT_EQ(found->id, account.id);
  EXPECT_TRUE(sg::common::verify_password(found->password_hash, "correct horse"));
  EXPECT_EQ(repo_->find_by_id(account.id)->handle, "ada_l");
  EXPECT_FALSE(repo_->find_by_handle("nobody"));
  EXPECT_FALSE(repo_->find_by_id(42));
}

TEST_F(IdentityPostgresTest, DuplicateHandleIs409AndWritesNoEvent) {
  (void)repo_->create(registration("ada_l"), hash());
  try {
    (void)repo_->create(registration("ada_l"), hash());
    FAIL() << "duplicate accepted";
  } catch (const ApiError& e) {
    EXPECT_EQ(e.status(), 409);
  }
  EXPECT_EQ(scalar<long>("SELECT count(*) FROM accounts"), 1);
  EXPECT_EQ(scalar<long>("SELECT count(*) FROM outbox"), 1);
}

TEST_F(IdentityPostgresTest, ConcurrentRegistrationsOfOneHandleCreateOneAccount) {
  std::atomic<int> created{0};
  std::atomic<int> conflicts{0};
  std::vector<std::thread> threads;
  const std::string password_hash = hash();
  for (int i = 0; i < 6; ++i) {
    threads.emplace_back([&] {
      try {
        (void)repo_->create(registration("racer"), password_hash);
        created.fetch_add(1);
      } catch (const ApiError& e) {
        if (e.status() == 409) {
          conflicts.fetch_add(1);
        }
      }
    });
  }
  for (auto& t : threads) {
    t.join();
  }
  EXPECT_EQ(created.load(), 1);
  EXPECT_EQ(conflicts.load(), 5);
  EXPECT_EQ(scalar<long>("SELECT count(*) FROM outbox"), 1);
}

TEST_F(IdentityPostgresTest, FailedOutboxWriteRollsBackTheAccount) {
  // Make the outbox insert fail inside the registration transaction.
  pool_->with_transaction([](pqxx::work& tx) {
    tx.exec("ALTER TABLE outbox ADD CONSTRAINT reject_all CHECK (type <> 'user.registered')");
  });
  EXPECT_ANY_THROW((void)repo_->create(registration("ada_l"), hash()));
  EXPECT_EQ(scalar<long>("SELECT count(*) FROM accounts"), 0);
  EXPECT_EQ(scalar<long>("SELECT count(*) FROM outbox"), 0);
}

TEST_F(IdentityPostgresTest, RolledBackTransactionLeavesNoOutboxRow) {
  EXPECT_THROW(pool_->with_transaction([](pqxx::work& tx) {
    (void)sg::common::enqueue_event(tx, "user.registered", json{{"user_id", 1}});
    throw std::runtime_error("abort");
  }),
               std::runtime_error);
  EXPECT_EQ(scalar<long>("SELECT count(*) FROM outbox"), 0);
}

TEST_F(IdentityPostgresTest, SchemaConstraintsHold) {
  const auto insert = [&](const std::string& handle, const std::string& name, const std::string& password_hash) {
    pool_->with_transaction([&](pqxx::work& tx) {
      tx.exec_params("INSERT INTO accounts (handle, name, password_hash) VALUES ($1, $2, $3)", handle, name,
                     password_hash);
    });
  };
  const std::string h = hash();
  EXPECT_THROW(insert("Bad Handle", "Name", h), pqxx::check_violation);
  EXPECT_THROW(insert("ab", "Name", h), pqxx::check_violation);
  EXPECT_THROW(insert("good_handle", "", h), pqxx::check_violation);
  EXPECT_THROW(insert("good_handle", std::string(65, 'n'), h), pqxx::check_violation);
  EXPECT_THROW(insert("good_handle", "Name", "plaintext-password"), pqxx::check_violation);
  EXPECT_NO_THROW(insert("good_handle", std::string(64, 'n'), h));
  EXPECT_THROW(insert("good_handle", "Name", h), pqxx::unique_violation);
}

TEST_F(IdentityPostgresTest, RecordLoginUpdatesTimestampAndHash) {
  const auto account = repo_->create(registration("ada_l"), hash());
  repo_->record_login(account.id, std::nullopt);
  const auto after_login = repo_->find_by_id(account.id);
  ASSERT_TRUE(after_login->last_login_at.has_value());
  EXPECT_EQ(after_login->password_hash, account.password_hash);
  const std::string upgraded = hash("correct horse");
  repo_->record_login(account.id, upgraded);
  EXPECT_EQ(repo_->find_by_id(account.id)->password_hash, upgraded);
}

TEST_F(IdentityPostgresTest, DemoAccountIsSeededOnceWithoutEvents) {
  EXPECT_THROW((void)sg::identity::infra::seed_demo_account_if_empty(*pool_, "short", PasswordHashParams::fast_for_tests()),
               std::invalid_argument);
  std::vector<std::thread> threads;
  std::atomic<int> seeded{0};
  for (int i = 0; i < 3; ++i) {
    threads.emplace_back([&] {
      if (sg::identity::infra::seed_demo_account_if_empty(*pool_, "demo-password", PasswordHashParams::fast_for_tests())) {
        seeded.fetch_add(1);
      }
    });
  }
  for (auto& t : threads) {
    t.join();
  }
  EXPECT_EQ(seeded.load(), 1);
  const auto demo = repo_->find_by_handle("demo");
  ASSERT_TRUE(demo);
  EXPECT_EQ(demo->id, 100000000);
  EXPECT_EQ(demo->name, "Demo Member");
  EXPECT_TRUE(sg::common::verify_password(demo->password_hash, "demo-password"));
  EXPECT_EQ(scalar<long>("SELECT count(*) FROM outbox"), 0);  // graph-service seeds the demo profile itself

  // Members continue after the demo id, and a second seed is a no-op.
  EXPECT_EQ(repo_->create(registration("ada_l"), hash()).id, 100000001);
  EXPECT_FALSE(sg::identity::infra::seed_demo_account_if_empty(*pool_, "demo-password", PasswordHashParams::fast_for_tests()));
}

TEST_F(IdentityPostgresTest, DemoSeedSkipsANonEmptyDatabase) {
  (void)repo_->create(registration("ada_l"), hash());
  EXPECT_FALSE(sg::identity::infra::seed_demo_account_if_empty(*pool_, "demo-password", PasswordHashParams::fast_for_tests()));
  EXPECT_FALSE(repo_->find_by_handle("demo"));
}

TEST_F(IdentityPostgresTest, AccountServiceEndToEndOnPostgres) {
  const auto keys = sg::common::generate_keypair();
  const sg::common::TokenIssuer issuer(keys.seed_b64, std::chrono::hours(1));
  const sg::identity::app::StaticTaxonomyProvider taxonomy(sg::identity::testing::test_taxonomy());
  sg::identity::app::AccountService service(*repo_, taxonomy, issuer, PasswordHashParams::fast_for_tests());
  sg::identity::domain::RegistrationInput in;
  in.handle = "Grace_H";
  in.name = "Grace Hopper";
  in.password = "cobol-rocks";
  in.category = "Databases";
  in.branch = "sql";
  const auto registered = service.register_account(in);
  const auto logged_in = service.login("grace_h", "cobol-rocks");
  EXPECT_EQ(logged_in.account.id, registered.account.id);
  EXPECT_TRUE(repo_->find_by_id(registered.account.id)->last_login_at.has_value());
  EXPECT_THROW((void)service.login("grace_h", "wrong-password"), ApiError);
}

TEST_F(IdentityPostgresTest, RelayPublishesUserRegisteredToRedis) {
  const auto redis_options = sg::common::testing::test_redis_options();
  if (!redis_options) {
    GTEST_SKIP() << "SG_TEST_REDIS_HOST not set";
  }
  const auto account = repo_->create(registration("ada_l"), hash());
  const std::string stream = sg::common::testing::unique_name("identity.events.test");
  sg::common::RedisClient redis(*redis_options);
  sg::common::OutboxRelay relay(*pool_, *redis_options, stream);
  EXPECT_EQ(relay.publish_batch_once(redis), 1U);
  EXPECT_EQ(relay.publish_batch_once(redis), 0U);  // marked published

  const auto entries = redis.xrange(stream);
  ASSERT_EQ(entries.size(), 1U);
  const auto event = sg::common::from_stream_entry(entries[0]);
  EXPECT_EQ(event.type, "user.registered");
  EXPECT_EQ(event.schema, 1);
  EXPECT_EQ(event.event_id, std::to_string(scalar<long>("SELECT id FROM outbox")));
  EXPECT_EQ(event.payload["user_id"], account.id);
  EXPECT_EQ(event.payload["handle"], "ada_l");
  EXPECT_EQ(scalar<long>("SELECT count(*) FROM outbox WHERE published_at IS NOT NULL"), 1);
  (void)redis.del(stream);
}

}  // namespace
