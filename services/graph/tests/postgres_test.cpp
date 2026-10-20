#include <algorithm>
#include <atomic>
#include <functional>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <sg/common/errors.hpp>
#include <sg/common/outbox.hpp>

#include "pg_fixture.hpp"
#include "sg/graph_service/app/graph_service.hpp"
#include "sg/graph_service/domain/events.hpp"

namespace {

using nlohmann::json;
using sg::common::ApiError;
namespace domain = sg::graph_service::domain;
using sg::graph_service::app::PoisonEvent;

constexpr std::int64_t kDemo = 100000000;

domain::NewMember member(std::int64_t id, const std::string& handle, const std::string& category = "Web Development",
                         const std::string& branch = "javascript") {
  domain::NewMember m;
  m.user_id = id;
  m.handle = handle;
  m.name = "Member " + handle;
  m.category = category;
  m.branch = branch;
  m.about = "hello";
  return m;
}

class GraphPostgresTest : public sg::graph_service::testing::GraphDatabaseTest {
 protected:
  void SetUp() override {
    GraphDatabaseTest::SetUp();
    if (IsSkipped()) {
      return;
    }
    ASSERT_TRUE(seed_fixtures().loaded);
  }

  int status_of(const std::function<void()>& f) {
    try {
      f();
    } catch (const ApiError& e) {
      return e.status();
    }
    return 0;
  }

  std::vector<json> outbox_payloads(const std::string& type) {
    return pool_->with_transaction([&](pqxx::work& tx) {
      std::vector<json> out;
      for (const auto& row : tx.exec_params("SELECT payload::text FROM outbox WHERE type = $1 ORDER BY id", type)) {
        out.push_back(json::parse(row[0].as<std::string>()));
      }
      return out;
    });
  }
};

TEST_F(GraphPostgresTest, AddConnectionBumpsVersionAndEnqueuesEventAtomically) {
  EXPECT_EQ(repo_->graph_version(), 1);
  const auto added = repo_->add_connection(kDemo, 101, 0.84049);
  EXPECT_EQ(added.graph_version, 2);
  EXPECT_DOUBLE_EQ(added.connection.strength, 0.84);
  EXPECT_EQ(added.connection.user.id, 101);
  EXPECT_EQ(added.connection.origin, domain::ConnectionOrigin::member);
  EXPECT_GT(added.connection.connected_at, 0);
  EXPECT_EQ(repo_->graph_version(), 2);
  EXPECT_EQ(outbox_payloads("connection.added"),
            std::vector<json>{json::parse(R"({"graph_version":2,"user_a":101,"user_b":100000000,"strength":0.84})")});
  EXPECT_EQ(scalar<std::string>("SELECT strength::text FROM connections WHERE user_a = 101 AND user_b = 100000000"),
            "0.840");
}

TEST_F(GraphPostgresTest, FailuresNeitherBumpTheVersionNorWriteEvents) {
  EXPECT_EQ(status_of([&] { (void)repo_->add_connection(kDemo, 104, 0.5); }), 409);  // seeded demo connection
  EXPECT_EQ(status_of([&] { (void)repo_->add_connection(kDemo, 999, 0.5); }), 404);
  EXPECT_EQ(status_of([&] { (void)repo_->add_connection(100000077, 101, 0.5); }), 404);
  EXPECT_EQ(status_of([&] { (void)repo_->remove_connection(kDemo, 101); }), 404);
  EXPECT_EQ(repo_->graph_version(), 1);
  EXPECT_EQ(scalar<long>("SELECT count(*) FROM outbox"), 0);
}

TEST_F(GraphPostgresTest, RemoveConnectionBumpsVersionAndEnqueuesEvent) {
  EXPECT_EQ(repo_->remove_connection(104, kDemo), 2);
  EXPECT_EQ(outbox_payloads("connection.removed"),
            std::vector<json>{json::parse(R"({"graph_version":2,"user_a":104,"user_b":100000000})")});
  EXPECT_EQ(scalar<long>("SELECT count(*) FROM connections WHERE user_b = 100000000"), 19);
}

TEST_F(GraphPostgresTest, RolledBackWriteLeavesNoOutboxRowAndNoVersionBump) {
  exec("ALTER TABLE outbox ADD CONSTRAINT reject_added CHECK (type <> 'connection.added')");
  EXPECT_ANY_THROW((void)repo_->add_connection(kDemo, 101, 0.5));
  EXPECT_EQ(repo_->graph_version(), 1);
  EXPECT_EQ(scalar<long>("SELECT count(*) FROM outbox"), 0);
  EXPECT_EQ(scalar<long>("SELECT count(*) FROM connections WHERE user_a = 101 AND user_b = 100000000"), 0);
}

TEST_F(GraphPostgresTest, ConcurrentWritesToOnePairStayConsistentUnderGroupCommit) {
  // Many threads toggle the same pair: every successful write gets its own version, and the event
  // stream alternates added/removed exactly like the table did.
  std::atomic<int> changes{0};
  std::vector<std::thread> threads;
  for (int t = 0; t < 8; ++t) {
    threads.emplace_back([&] {
      for (int k = 0; k < 10; ++k) {
        try {
          (void)repo_->add_connection(kDemo, 101, 0.5);
          changes.fetch_add(1);
        } catch (const ApiError& e) {
          EXPECT_EQ(e.status(), 409);
        }
        try {
          (void)repo_->remove_connection(101, kDemo);
          changes.fetch_add(1);
        } catch (const ApiError& e) {
          EXPECT_EQ(e.status(), 404);
        }
      }
    });
  }
  for (auto& t : threads) {
    t.join();
  }
  EXPECT_EQ(repo_->graph_version(), 1 + changes.load());
  const auto events = scalar<long>("SELECT count(*) FROM outbox");
  EXPECT_EQ(events, changes.load());
  std::string expected = "connection.added";
  long version = 2;
  pool_->with_transaction([&](pqxx::work& tx) {
    for (const auto& row : tx.exec("SELECT type, (payload->>'graph_version')::bigint FROM outbox ORDER BY id")) {
      EXPECT_EQ(row[0].as<std::string>(), expected);
      EXPECT_EQ(row[1].as<long>(), version++);
      expected = expected == "connection.added" ? "connection.removed" : "connection.added";
    }
  });
}

TEST_F(GraphPostgresTest, StoredConnectionCountsMatchTheConnections) {
  constexpr const char* kDrift =
      "SELECT count(*) FROM profiles p WHERE p.connection_count <> "
      "(SELECT count(*) FROM connections c WHERE c.user_a = p.user_id OR c.user_b = p.user_id)";
  EXPECT_EQ(scalar<long>(kDrift), 0) << "after the seed import";
  (void)repo_->add_connection(kDemo, 101, 0.5);
  (void)repo_->add_connection(kDemo, 102, 0.5);
  (void)repo_->remove_connection(kDemo, 104);
  EXPECT_EQ(scalar<long>(kDrift), 0) << "after writes";
  EXPECT_EQ(repo_->find_profile(kDemo)->connection_count, 21);
}

TEST_F(GraphPostgresTest, ConcurrentWritesProduceGapFreeCommitOrderedVersions) {
  // 8 writers x 6 connections on distinct pairs, plus concurrent failing writes (duplicates).
  std::atomic<int> failures{0};
  std::vector<std::thread> threads;
  for (int t = 0; t < 8; ++t) {
    threads.emplace_back([&, t] {
      for (int k = 0; k < 6; ++k) {
        try {
          (void)repo_->add_connection(104 + t, 112 + k, 0.5);
          (void)repo_->add_connection(kDemo, 104 + t, 0.5);  // always 409: seeded
        } catch (const ApiError& e) {
          if (e.status() == 409) {
            failures.fetch_add(1);
          }
        }
      }
    });
  }
  for (auto& t : threads) {
    t.join();
  }
  EXPECT_EQ(failures.load(), 48);
  EXPECT_EQ(repo_->graph_version(), 1 + 48);
  const auto payloads = outbox_payloads("connection.added");  // ordered by outbox id
  ASSERT_EQ(payloads.size(), 48U);
  for (std::size_t i = 0; i < payloads.size(); ++i) {
    EXPECT_EQ(payloads[i]["graph_version"], static_cast<std::int64_t>(i) + 2) << "outbox order != version order";
  }
}

TEST_F(GraphPostgresTest, MemberProfileEventIsAppliedExactlyOnce) {
  const auto m = member(100000001, "ada_l");
  EXPECT_TRUE(repo_->create_member_profile(m, "identity.events", "41"));
  EXPECT_FALSE(repo_->create_member_profile(m, "identity.events", "41"));  // redelivery
  EXPECT_FALSE(repo_->create_member_profile(m, "identity.events", "42"));  // republished under a new id
  EXPECT_EQ(repo_->graph_version(), 2);
  const auto created = outbox_payloads("user.created");
  ASSERT_EQ(created.size(), 1U);
  EXPECT_EQ(created[0]["graph_version"], 2);
  const json& user = created[0]["user"];
  EXPECT_EQ(user["id"], 100000001);
  EXPECT_EQ(user["source"], "member");
  EXPECT_EQ(user["reputation"], 1);
  EXPECT_EQ(user["influence"], 0.0);
  EXPECT_EQ(user["about"], "hello");
  EXPECT_TRUE(user["external_url"].is_null());
  EXPECT_EQ(user.size(), 17U);
  const auto found = repo_->find_profile(100000001);
  ASSERT_TRUE(found);
  EXPECT_EQ(domain::to_json(found->profile), user);
}

TEST_F(GraphPostgresTest, RegistrationsTheSchemaRejectsArePoisonAndRolledBack) {
  EXPECT_THROW((void)repo_->create_member_profile(member(100000001, "ada_l", "Web Development", "cobol"), "s", "1"),
               PoisonEvent);
  EXPECT_THROW((void)repo_->create_member_profile(member(42, "low_id"), "s", "2"), PoisonEvent);  // id space
  EXPECT_EQ(repo_->graph_version(), 1);
  EXPECT_EQ(scalar<long>("SELECT count(*) FROM processed_events"), 0);
  EXPECT_EQ(scalar<long>("SELECT count(*) FROM outbox"), 0);
}

TEST_F(GraphPostgresTest, MemberMayTakeTheHandleOfAnImportedUser) {
  EXPECT_TRUE(repo_->create_member_profile(member(100000001, "js_user_101"), "s", "1"));
  EXPECT_THROW((void)repo_->create_member_profile(member(100000002, "js_user_101"), "s", "2"), PoisonEvent);
}

TEST_F(GraphPostgresTest, SchemaConstraintsHold) {
  const auto rejects = [&](const std::string& sql) {
    EXPECT_THROW(exec(sql), pqxx::integrity_constraint_violation) << sql;
  };
  rejects("INSERT INTO connections (user_a, user_b, strength, origin) VALUES (102, 101, 0.5, 'member')");
  rejects("INSERT INTO connections (user_a, user_b, strength, origin) VALUES (101, 101, 0.5, 'member')");
  rejects("INSERT INTO connections (user_a, user_b, strength, origin) VALUES (101, 999, 0.5, 'member')");
  rejects("INSERT INTO connections (user_a, user_b, strength, origin) VALUES (101, 103, 1.5, 'member')");
  rejects("INSERT INTO connections (user_a, user_b, strength, origin) VALUES (101, 103, 0.5, 'friend')");
  rejects("UPDATE profiles SET influence = 100.01 WHERE user_id = 101");
  rejects("UPDATE profiles SET influence = -1 WHERE user_id = 101");
  rejects("UPDATE profiles SET handle = 'Bad Handle' WHERE user_id = 101");
  rejects("UPDATE profiles SET branch = 'cobol' WHERE user_id = 101");
  rejects("UPDATE profiles SET source = 'member' WHERE user_id = 101");  // imported id space
  rejects("UPDATE profiles SET about = repeat('a', 501) WHERE user_id = 101");
  rejects("INSERT INTO graph_meta (id, version) VALUES (false, 0)");

  // Deleting a profile removes its connections.
  EXPECT_EQ(scalar<long>("SELECT count(*) FROM connections WHERE user_a = 101 OR user_b = 101"), 2);
  exec("DELETE FROM profiles WHERE user_id = 101");
  EXPECT_EQ(scalar<long>("SELECT count(*) FROM connections WHERE user_a = 101 OR user_b = 101"), 0);
}

TEST_F(GraphPostgresTest, ListProfilesFiltersSortsAndCounts) {
  domain::UserQuery q;
  auto page = repo_->list_profiles(q);
  EXPECT_EQ(page.total, 32);  // 31 imported + demo
  ASSERT_EQ(page.items.size(), 20U);
  EXPECT_EQ(page.items[0].profile.handle, "jon_skeet_303");
  EXPECT_EQ(page.items[1].profile.handle, "sql_guru_301");
  EXPECT_EQ(page.items[0].connection_count, 1);
  EXPECT_EQ(page.items[1].connection_count, 2);

  q.offset = 30;
  page = repo_->list_profiles(q);
  ASSERT_EQ(page.items.size(), 2U);
  EXPECT_EQ(page.items[1].profile.id, kDemo);  // reputation 1, last
  EXPECT_EQ(page.items[1].connection_count, 20);

  q.offset = 40;  // past the end: no items, but still the total
  page = repo_->list_profiles(q);
  EXPECT_TRUE(page.items.empty());
  EXPECT_EQ(page.total, 32);

  q = {};
  q.q = "JON";  // handle or name, case-insensitive
  page = repo_->list_profiles(q);
  EXPECT_EQ(page.total, 2);
  EXPECT_EQ(page.items[0].profile.id, 303);
  EXPECT_EQ(page.items[1].profile.id, 402);

  q.q = "Schö";  // non-ASCII name
  EXPECT_EQ(repo_->list_profiles(q).items.at(0).profile.id, 102);
  q.q = "%";  // LIKE wildcards are literal
  EXPECT_EQ(repo_->list_profiles(q).total, 1);  // "100% Coverage"
  q.q = "_3";  // "_" is literal too: only sql_guru_301, dba_302 and jon_skeet_303
  page = repo_->list_profiles(q);
  EXPECT_EQ(page.total, 3);
  for (const auto& item : page.items) {
    EXPECT_NE(item.profile.handle.find("_3"), std::string::npos);
  }

  q = {};
  q.category = "Databases & Data";
  q.sort = domain::UserSort::id;
  page = repo_->list_profiles(q);
  EXPECT_EQ(page.total, 3);
  EXPECT_EQ(page.items[0].profile.id, 301);

  q = {};
  q.branch = "javascript";
  q.min_reputation = 22000;
  page = repo_->list_profiles(q);
  EXPECT_EQ(page.total, 2);
  EXPECT_EQ(page.items[0].profile.id, 123);

  q = {};
  q.sort = domain::UserSort::name;
  q.limit = 3;
  page = repo_->list_profiles(q);
  EXPECT_EQ(page.items[0].profile.name, "100% Coverage");
  EXPECT_EQ(page.items[1].profile.name, "Cpp Dev");
  EXPECT_EQ(page.items[2].profile.name, "Dee Bee");
}

TEST_F(GraphPostgresTest, ListConnectionsIsSortedAndVersioned) {
  const auto list = repo_->list_connections(301);
  EXPECT_EQ(list.graph_version, 1);
  ASSERT_EQ(list.items.size(), 2U);
  EXPECT_EQ(list.items[0].user.id, 102);  // 0.5
  EXPECT_EQ(list.items[1].user.id, 401);  // 0.142
  EXPECT_DOUBLE_EQ(list.items[1].strength, 0.142);
  EXPECT_EQ(list.items[1].origin, domain::ConnectionOrigin::imported);
  EXPECT_EQ(list.items[1].interactions, 1);
  EXPECT_EQ(list.items[1].connected_at, list.items[1].first_interaction_at.value());
  EXPECT_FALSE(list.items[1].last_interaction_at.has_value());

  const auto demo = repo_->list_connections(kDemo);
  ASSERT_EQ(demo.items.size(), 20U);
  EXPECT_TRUE(std::is_sorted(demo.items.begin(), demo.items.end(), [](const auto& a, const auto& b) {
    return a.strength != b.strength ? a.strength > b.strength : a.user.id < b.user.id;
  }));
}

TEST_F(GraphPostgresTest, SnapshotIsConsistent) {
  (void)repo_->add_connection(kDemo, 101, 0.5);
  const auto snapshot = repo_->snapshot();
  EXPECT_EQ(snapshot.graph_version, 2);
  EXPECT_EQ(snapshot.users.size(), 32U);
  EXPECT_EQ(snapshot.edges.size(), 6U + 20U + 1U);
  std::set<std::int64_t> ids;
  for (const auto& u : snapshot.users) {
    ids.insert(u.id);
  }
  for (const auto& e : snapshot.edges) {
    EXPECT_LT(e.a, e.b);
    EXPECT_TRUE(ids.contains(e.a) && ids.contains(e.b));
  }
  EXPECT_TRUE(std::is_sorted(snapshot.users.begin(), snapshot.users.end(),
                             [](const auto& a, const auto& b) { return a.id < b.id; }));
}

TEST_F(GraphPostgresTest, ServiceConnectStoresAffinityStrength) {
  sg::graph_service::app::GraphService graph(*repo_);
  ASSERT_TRUE(repo_->create_member_profile(member(100000001, "ada_l", "Systems Programming", "c++"), "s", "1"));
  const auto added = graph.connect(100000001, 401);  // same category and branch, influence 0 vs 60
  EXPECT_DOUBLE_EQ(added.connection.strength, 0.82);
  EXPECT_EQ(added.graph_version, 3);
  EXPECT_EQ(graph.my_connections(100000001).items.at(0).user.handle, "cpp_dev_401");
}

}  // namespace
