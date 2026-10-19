#include "fixtures.hpp"
#include "read_model.hpp"

#include <gtest/gtest.h>

#include <thread>

namespace sg::analytics {
namespace {

using namespace std::chrono_literals;
using testing::connection_added;
using testing::connection_removed;
using testing::load_two_cliques;
using testing::make_user;
using testing::user_created;

TEST(ReadModel, NotReadyUntilReset) {
  ReadModel model;
  EXPECT_FALSE(model.ready());
  load_two_cliques(model);
  EXPECT_TRUE(model.ready());
  EXPECT_EQ(model.version(), 10U);
  auto state = model.materialize();
  EXPECT_EQ(state->users.size(), 9U);
  EXPECT_EQ(state->edges.size(), 13U);
  EXPECT_EQ(state->users.front().id, 1);  // ordered by id
}

TEST(ReadModel, AppliesEventsStrictlyInOrder) {
  ReadModel model;
  load_two_cliques(model);
  EXPECT_EQ(model.apply(connection_added(11, 9, 1)), ApplyResult::applied);
  EXPECT_EQ(model.apply(connection_added(11, 9, 1)), ApplyResult::duplicate);
  EXPECT_EQ(model.apply(connection_added(5, 9, 2)), ApplyResult::duplicate);
  EXPECT_EQ(model.apply(connection_added(13, 9, 2)), ApplyResult::gap);
  EXPECT_EQ(model.version(), 11U);
  EXPECT_EQ(model.materialize()->edges.size(), 14U);
}

TEST(ReadModel, NormalisesEdgeOrientationAndRemoves) {
  ReadModel model;
  load_two_cliques(model);
  ASSERT_EQ(model.apply(connection_added(11, 9, 3)), ApplyResult::applied);  // reported as (9,3)
  auto state = model.materialize();
  auto it = std::find_if(state->edges.begin(), state->edges.end(),
                         [](const EdgeInfo& e) { return e.user_a == 3 && e.user_b == 9; });
  ASSERT_NE(it, state->edges.end());
  ASSERT_EQ(model.apply(connection_removed(12, 3, 9)), ApplyResult::applied);
  EXPECT_EQ(model.materialize()->edges.size(), 13U);
}

TEST(ReadModel, UserCreatedAddsNode) {
  ReadModel model;
  load_two_cliques(model);
  ASSERT_EQ(model.apply(user_created(11, make_user(42, "HR", "human resources", 77.0))), ApplyResult::applied);
  auto state = model.materialize();
  ASSERT_EQ(state->users.size(), 10U);
  EXPECT_EQ(state->users.back().id, 42);
  EXPECT_EQ(state->users.back().category, "HR");
}

TEST(ReadModel, ConnectionToUnknownUserForcesResync) {
  ReadModel model;
  load_two_cliques(model);
  EXPECT_EQ(model.apply(connection_added(11, 1, 999)), ApplyResult::gap);
  EXPECT_EQ(model.version(), 10U);
}

TEST(ReadModel, UnknownEventTypesAreIgnored) {
  ReadModel model;
  load_two_cliques(model);
  sg::common::Event event{"1", "user.renamed", 1, "2026-10-19T00:00:00Z", {{"graph_version", 11}}};
  EXPECT_EQ(model.apply(event), ApplyResult::ignored);
  EXPECT_EQ(model.version(), 10U);
}

TEST(ReadModel, MalformedPayloadThrows) {
  ReadModel model;
  load_two_cliques(model);
  sg::common::Event event{"1", "connection.added", 1, "2026-10-19T00:00:00Z", {{"graph_version", 11}}};
  EXPECT_ANY_THROW(model.apply(event));
  EXPECT_EQ(model.version(), 10U);
}

TEST(ReadModel, ResetRejectsDanglingEdges) {
  ReadModel model;
  EXPECT_THROW(model.reset(1, {make_user(1)}, {{1, 2, 0.5}}), std::invalid_argument);
}

TEST(ReadModel, WaitForVersion) {
  ReadModel model;
  load_two_cliques(model);
  EXPECT_TRUE(model.wait_for_version(10, 0ms));
  EXPECT_FALSE(model.wait_for_version(11, 20ms));
  std::jthread writer([&] {
    std::this_thread::sleep_for(30ms);
    model.apply(connection_added(11, 9, 1));
  });
  EXPECT_TRUE(model.wait_for_version(11, 5s));
}

TEST(Snapshot, ParsesGraphServiceBody) {
  nlohmann::json body = {{"graph_version", 7},
                         {"users", {testing::user_json(make_user(1)), testing::user_json(make_user(2))}},
                         {"edges", {{2, 1, 0.75}}}};
  auto snapshot = parse_snapshot(body);
  EXPECT_EQ(snapshot.version, 7U);
  ASSERT_EQ(snapshot.edges.size(), 1U);
  EXPECT_EQ(snapshot.edges[0].user_a, 1);
  EXPECT_EQ(snapshot.edges[0].user_b, 2);
  EXPECT_DOUBLE_EQ(snapshot.edges[0].strength, 0.75);
}

TEST(Snapshot, AcceptsInfluenceAsDecimalString) {
  auto json = testing::user_json(make_user(1));
  json["influence"] = "72.50";
  EXPECT_DOUBLE_EQ(parse_user(json).influence, 72.5);
}

}  // namespace
}  // namespace sg::analytics
