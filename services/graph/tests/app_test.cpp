#include <functional>
#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <sg/common/errors.hpp>
#include <sg/common/events.hpp>
#include <sg/graph/affinity.hpp>

#include "fakes.hpp"
#include "sg/graph_service/app/graph_service.hpp"

namespace {

using nlohmann::json;
using sg::common::ApiError;
using sg::graph_service::app::EventOutcome;
using sg::graph_service::app::GraphService;
using sg::graph_service::app::PoisonEvent;
using sg::graph_service::testing::InMemoryGraphRepository;

constexpr std::int64_t kAda = 100000001;

ApiError error_of(const std::function<void()>& f) {
  try {
    f();
  } catch (const ApiError& e) {
    return e;
  }
  ADD_FAILURE() << "no ApiError thrown";
  return sg::common::internal_error();
}

sg::common::Event registered_event(const std::string& event_id, std::int64_t user_id, const std::string& handle,
                                   const std::string& branch = "javascript") {
  sg::common::Event e;
  e.event_id = event_id;
  e.type = "user.registered";
  e.occurred_at = "2026-10-19T20:00:00Z";
  e.payload = {{"user_id", user_id}, {"handle", handle},     {"name", "New Member"}, {"category", "Web Development"},
               {"branch", branch},   {"location", "Berlin"}, {"about", nullptr}};
  return e;
}

class GraphServiceTest : public ::testing::Test {
 protected:
  void SetUp() override { sg::graph_service::testing::populate(repo); }
  InMemoryGraphRepository repo;
  GraphService graph{repo};
};

TEST_F(GraphServiceTest, TaxonomyIsCachedOnceLoaded) {
  EXPECT_EQ(graph.taxonomy()->categories.size(), 3U);
  (void)graph.taxonomy();
  EXPECT_EQ(repo.taxonomy_loads(), 1);

  InMemoryGraphRepository empty;
  empty.set_taxonomy({});
  GraphService unseeded(empty);
  EXPECT_TRUE(unseeded.taxonomy()->empty());
  (void)unseeded.taxonomy();
  EXPECT_EQ(empty.taxonomy_loads(), 2);  // an empty taxonomy is not cached
}

TEST_F(GraphServiceTest, ProfilesAnd404s) {
  EXPECT_EQ(graph.profile(2).connection_count, 2);
  ApiError e = error_of([&] { (void)graph.profile(999); });
  EXPECT_EQ(e.status(), 404);
  EXPECT_EQ(graph.my_profile(kAda).profile.handle, "ada_l");
  e = error_of([&] { (void)graph.my_profile(100000002); });
  EXPECT_EQ(e.status(), 404);
  EXPECT_NE(e.message().find("provisioned"), std::string::npos);
}

TEST_F(GraphServiceTest, UnknownFiltersAre400) {
  sg::graph_service::domain::UserQuery q;
  q.category = "Engineering";
  EXPECT_EQ(error_of([&] { (void)graph.list_users(q); }).status(), 400);
  q.category = "Databases & Data";
  q.branch = "cobol";
  EXPECT_EQ(error_of([&] { (void)graph.list_users(q); }).status(), 400);
  q.branch = "sql";
  EXPECT_EQ(graph.list_users(q).total, 2);
}

TEST_F(GraphServiceTest, ConnectComputesAffinityAndBumpsTheVersion) {
  const auto added = graph.connect(kAda, 1);
  // Same category and branch, influence 0 vs 97.03: 0.4 + 0.3 + 0.3 * (1 - 0.9703).
  EXPECT_DOUBLE_EQ(added.connection.strength, sg::graph::connection_strength({0, 0, 0.0}, {0, 0, 97.03}));
  EXPECT_DOUBLE_EQ(added.connection.strength, 0.709);
  EXPECT_EQ(added.connection.user.id, 1);
  EXPECT_EQ(added.graph_version, 2);
  const auto events = repo.events();
  ASSERT_EQ(events.size(), 1U);
  EXPECT_EQ(events[0].type, "connection.added");
  EXPECT_EQ(events[0].payload, json::parse(R"({"graph_version":2,"user_a":1,"user_b":100000001,"strength":0.709})"));

  const auto list = graph.my_connections(kAda);
  ASSERT_EQ(list.items.size(), 1U);
  EXPECT_EQ(list.graph_version, 2);
}

TEST_F(GraphServiceTest, ConnectErrors) {
  ApiError e = error_of([&] { (void)graph.connect(kAda, kAda); });
  EXPECT_EQ(e.status(), 422);
  ASSERT_EQ(e.details().size(), 1U);
  EXPECT_EQ(e.details()[0].field, "user_id");
  EXPECT_EQ(error_of([&] { (void)graph.connect(kAda, 999); }).status(), 404);
  EXPECT_EQ(error_of([&] { (void)graph.connect(100000002, 1); }).status(), 404);  // caller not provisioned
  (void)graph.connect(kAda, 1);
  e = error_of([&] { (void)graph.connect(1, kAda); });  // either direction
  EXPECT_EQ(e.status(), 409);
  EXPECT_EQ(repo.version(), 2);  // failures never bump the version
  EXPECT_EQ(repo.events().size(), 1U);
}

TEST_F(GraphServiceTest, DisconnectRemovesOrIs404) {
  EXPECT_EQ(error_of([&] { (void)graph.disconnect(kAda, 1); }).status(), 404);
  (void)graph.connect(kAda, 1);
  EXPECT_EQ(graph.disconnect(kAda, 1), 3);
  EXPECT_EQ(repo.events().back().payload, json::parse(R"({"graph_version":3,"user_a":1,"user_b":100000001})"));
  EXPECT_EQ(error_of([&] { (void)graph.disconnect(kAda, 1); }).status(), 404);
  EXPECT_EQ(repo.version(), 3);
}

TEST_F(GraphServiceTest, MyConnectionsNeedsAProfileAndIsSortedByStrength) {
  EXPECT_EQ(error_of([&] { (void)graph.my_connections(100000002); }).status(), 404);
  const auto list = graph.my_connections(2);
  ASSERT_EQ(list.items.size(), 2U);
  EXPECT_EQ(list.items[0].user.id, 3);  // 0.661
  EXPECT_EQ(list.items[1].user.id, 1);  // 0.255
}

TEST_F(GraphServiceTest, UserRegisteredCreatesTheProfileOnce) {
  const auto event = registered_event("17", 100000005, "new_member");
  EXPECT_EQ(graph.handle_identity_event(event, "identity.events"), EventOutcome::applied);
  EXPECT_EQ(graph.handle_identity_event(event, "identity.events"), EventOutcome::duplicate);
  EXPECT_EQ(graph.handle_identity_event(registered_event("18", 100000005, "new_member"), "identity.events"),
            EventOutcome::duplicate);  // same user under another event id
  const auto profile = graph.profile(100000005).profile;
  EXPECT_EQ(profile.location, "Berlin");
  EXPECT_EQ(profile.reputation, 1);
  EXPECT_EQ(repo.version(), 2);
  const auto events = repo.events();
  ASSERT_EQ(events.size(), 1U);
  EXPECT_EQ(events[0].type, "user.created");
  EXPECT_EQ(events[0].payload["graph_version"], 2);
  EXPECT_EQ(events[0].payload["user"]["source"], "member");
}

TEST_F(GraphServiceTest, OtherEventTypesAreIgnored) {
  auto event = registered_event("1", 100000005, "x_member");
  event.type = "user.deleted";
  EXPECT_EQ(graph.handle_identity_event(event, "identity.events"), EventOutcome::ignored);
  EXPECT_EQ(repo.version(), 1);
}

TEST_F(GraphServiceTest, InvalidRegistrationsArePoison) {
  auto malformed = registered_event("1", 100000005, "x_member");
  malformed.payload.erase("handle");
  EXPECT_THROW((void)graph.handle_identity_event(malformed, "identity.events"), PoisonEvent);
  EXPECT_THROW((void)graph.handle_identity_event(registered_event("2", 100000006, "y_member", "cobol"), "identity.events"),
               PoisonEvent);
  EXPECT_EQ(repo.version(), 1);
}

TEST(GraphServiceUnseeded, RegistrationsWaitForTheTaxonomy) {
  InMemoryGraphRepository repo;
  repo.set_taxonomy({});
  GraphService graph(repo);
  const ApiError e = error_of([&] { (void)graph.handle_identity_event(registered_event("1", 100000005, "x_member"), "s"); });
  EXPECT_EQ(e.status(), 503);  // transient: retried, never dead-lettered
}

}  // namespace
