// Cross-service event flow: identity-service registers a member (application layer + PostgreSQL
// outbox), its relay publishes user.registered to Redis, graph-service's consumer creates the
// profile, and graph's relay publishes user.created carrying the full UserProfile.

#include <chrono>
#include <memory>
#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <sg/common/auth.hpp>
#include <sg/common/crypto.hpp>
#include <sg/common/events.hpp>
#include <sg/common/outbox.hpp>
#include <sg/common/redis.hpp>
#include <sg/common/testing/test_env.hpp>

#include "pg_fixture.hpp"
#include "sg/graph_service/app/graph_service.hpp"
#include "sg/graph_service/infra/identity_events_consumer.hpp"
#include "sg/identity/app/account_service.hpp"
#include "sg/identity/app/taxonomy_provider.hpp"
#include "sg/identity/infra/migrations.hpp"
#include "sg/identity/infra/postgres_account_repository.hpp"

namespace {

using namespace std::chrono_literals;
using nlohmann::json;

class EventFlowTest : public sg::graph_service::testing::GraphDatabaseTest {
 protected:
  void SetUp() override {
    redis_options_ = sg::common::testing::test_redis_options();
    if (!redis_options_) {
      GTEST_SKIP() << "SG_TEST_REDIS_HOST not set";
    }
    GraphDatabaseTest::SetUp();  // graph database (migrated)
    if (IsSkipped()) {
      return;
    }
    ASSERT_TRUE(seed_fixtures().loaded);
    identity_db_ = std::make_unique<sg::common::testing::TempDatabase>(*sg::common::testing::test_database_url(),
                                                                        "sg_identity_flow");
    sg::common::PgPoolOptions o;
    o.conninfo = identity_db_->url();
    o.size = 4;
    identity_pool_ = std::make_unique<sg::common::PgPool>(o);
    sg::common::run_migrations(*identity_pool_, sg::identity::infra::embedded_migrations(),
                               sg::identity::infra::kMigrationLockKey);
    redis_ = std::make_unique<sg::common::RedisClient>(*redis_options_);
    identity_stream_ = sg::common::testing::unique_name("identity.events.flow");
    graph_stream_ = sg::common::testing::unique_name("graph.events.flow");
  }
  void TearDown() override {
    if (redis_) {
      (void)redis_->del(identity_stream_);
      (void)redis_->del(graph_stream_);
    }
  }

  std::optional<sg::common::RedisOptions> redis_options_;
  std::unique_ptr<sg::common::testing::TempDatabase> identity_db_;
  std::unique_ptr<sg::common::PgPool> identity_pool_;
  std::unique_ptr<sg::common::RedisClient> redis_;
  std::string identity_stream_;
  std::string graph_stream_;
};

TEST_F(EventFlowTest, RegistrationBecomesAGraphProfileAndUserCreatedEvent) {
  // identity-service: validate against graph's taxonomy, store account + outbox row.
  sg::identity::domain::Taxonomy taxonomy;
  for (const auto& c : repo_->load_taxonomy().categories) {
    taxonomy.categories.push_back(c.name);
  }
  for (const auto& b : repo_->load_taxonomy().branches) {
    taxonomy.branches.push_back(b.name);
  }
  const sg::identity::app::StaticTaxonomyProvider taxonomy_provider(taxonomy);
  const auto keys = sg::common::generate_keypair();
  const sg::common::TokenIssuer issuer(keys.seed_b64, std::chrono::hours(1));
  sg::identity::infra::PostgresAccountRepository accounts(*identity_pool_);
  sg::identity::app::AccountService identity(accounts, taxonomy_provider, issuer,
                                             sg::common::PasswordHashParams::fast_for_tests());
  sg::identity::domain::RegistrationInput input;
  input.handle = "Grace_H";
  input.name = "Grace Hopper";
  input.password = "cobol-is-fine";
  input.category = "Databases & Data";
  input.branch = "sql";
  input.location = "Arlington, VA";
  const auto registered = identity.register_account(input);
  EXPECT_EQ(registered.account.id, 100000001);

  // identity relay -> identity.events
  sg::common::OutboxRelay identity_relay(*identity_pool_, *redis_options_, identity_stream_);
  ASSERT_EQ(identity_relay.publish_batch_once(*redis_), 1U);

  // graph consumer (group graph-service) -> profile + user.created in graph's outbox
  sg::graph_service::app::GraphService graph(*repo_);
  sg::graph_service::infra::IdentityEventsConsumer::Options options;
  options.stream = identity_stream_;
  options.consumer = "flow-test";
  options.block = 50ms;
  sg::graph_service::infra::IdentityEventsConsumer consumer(graph, *redis_options_, options);
  ASSERT_EQ(consumer.poll_once(*redis_, 50ms), 1U);

  const auto profile = graph.my_profile(registered.account.id);
  EXPECT_EQ(profile.profile.handle, "grace_h");
  EXPECT_EQ(profile.profile.name, "Grace Hopper");
  EXPECT_EQ(profile.profile.branch, "sql");
  EXPECT_EQ(profile.profile.location, "Arlington, VA");
  EXPECT_EQ(profile.connection_count, 0);

  // graph relay -> graph.events
  sg::common::OutboxRelay graph_relay(*pool_, *redis_options_, graph_stream_);
  ASSERT_EQ(graph_relay.publish_batch_once(*redis_), 1U);
  const auto entries = redis_->xrange(graph_stream_);
  ASSERT_EQ(entries.size(), 1U);
  const auto event = sg::common::from_stream_entry(entries[0]);
  EXPECT_EQ(event.type, "user.created");
  EXPECT_EQ(event.payload["graph_version"], 2);  // seed = 1
  const json& user = event.payload["user"];
  EXPECT_EQ(user["id"], registered.account.id);
  EXPECT_EQ(user["handle"], "grace_h");
  EXPECT_EQ(user["category"], "Databases & Data");
  EXPECT_EQ(user["source"], "member");
  EXPECT_EQ(user["reputation"], 1);
  EXPECT_TRUE(user["external_url"].is_null());

  // A second delivery of the same identity event changes nothing.
  const auto identity_entries = redis_->xrange(identity_stream_);
  ASSERT_EQ(identity_entries.size(), 1U);
  (void)redis_->xadd(identity_stream_, std::vector<std::pair<std::string, std::string>>(
                                           identity_entries[0].fields.begin(), identity_entries[0].fields.end()));
  EXPECT_EQ(consumer.poll_once(*redis_, 50ms), 1U);
  EXPECT_EQ(repo_->graph_version(), 2);
  EXPECT_EQ(graph_relay.publish_batch_once(*redis_), 0U);
}

}  // namespace
