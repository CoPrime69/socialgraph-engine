#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <sg/graph/affinity.hpp>

#include "fakes.hpp"
#include "sg/graph_service/domain/affinity.hpp"
#include "sg/graph_service/domain/events.hpp"
#include "sg/graph_service/domain/profile.hpp"
#include "sg/graph_service/domain/taxonomy.hpp"
#include "sg/graph_service/domain/user_query.hpp"

namespace {

using nlohmann::json;
namespace domain = sg::graph_service::domain;
using sg::graph_service::testing::imported_profile;
using sg::graph_service::testing::test_taxonomy;

json registered(std::int64_t id = 100000001) {
  return {{"user_id", id},        {"handle", "ada_l"},       {"name", "  Ada Lovelace "},
          {"category", "Web Development"}, {"branch", "javascript"}, {"location", nullptr},
          {"about", "Line 1\nLine 2"}};
}

TEST(ProfileJson, ImportedUserMatchesTheContract) {
  auto p = imported_profile(1, "jeff_atwood_1", "Jeff Atwood", "Web Development", "javascript", 97.03, 63708);
  p.location = "El Cerrito, CA";
  p.views = 596583;
  p.up_votes = 3270;
  p.down_votes = 1108;
  EXPECT_EQ(domain::to_json(p), json::parse(R"({
    "id": 1, "handle": "jeff_atwood_1", "name": "Jeff Atwood", "category": "Web Development",
    "branch": "javascript", "influence": 97.03, "reputation": 63708, "location": "El Cerrito, CA",
    "website_url": null, "about": null, "views": 596583, "up_votes": 3270, "down_votes": 1108,
    "source": "stackoverflow", "external_url": "https://stackoverflow.com/users/1",
    "created_at": "2008-08-01T00:00:00Z", "last_access_at": "2024-01-01T00:00:00Z"})"));
}

TEST(ProfileJson, MemberHasNoExternalUrlAndCountIsAdded) {
  domain::NewMember m;
  m.user_id = 100000001;
  m.handle = "ada_l";
  m.name = "Ada";
  m.category = "Web Development";
  m.branch = "javascript";
  const auto p = domain::member_profile(m, 1760904000);
  EXPECT_EQ(p.reputation, 1);
  EXPECT_EQ(p.influence, 0.0);
  const json j = domain::to_json(domain::ProfileWithCount{p, 7});
  EXPECT_EQ(j["source"], "member");
  EXPECT_TRUE(j["external_url"].is_null());
  EXPECT_TRUE(j["last_access_at"].is_null());
  EXPECT_EQ(j["created_at"], "2025-10-19T20:00:00Z");
  EXPECT_EQ(j["connection_count"], 7);
  EXPECT_EQ(j.size(), 18U);
}

TEST(ConnectionJson, MatchesTheContract) {
  domain::ConnectionItem c;
  c.user = imported_profile(2, "geoff_2", "Geoff", "Databases & Data", "sql", 72.42, 6156);
  c.strength = 0.66149999;
  c.interactions = 3;
  c.origin = domain::ConnectionOrigin::imported;
  c.connected_at = 1219348431;
  c.first_interaction_at = 1219348431;
  const json j = domain::to_json(c);
  EXPECT_EQ(j["strength"], 0.661);
  EXPECT_EQ(j["interactions"], 3);
  EXPECT_EQ(j["origin"], "imported");
  EXPECT_EQ(j["connected_at"], "2008-08-21T19:53:51Z");
  EXPECT_EQ(j["first_interaction_at"], "2008-08-21T19:53:51Z");
  EXPECT_TRUE(j["last_interaction_at"].is_null());
  EXPECT_EQ(j["user"]["id"], 2);
}

TEST(Taxonomy, CodesAreDisplayOrderIndexes) {
  const auto t = test_taxonomy();
  EXPECT_EQ(t.category_code("Web Development"), 0);
  EXPECT_EQ(t.category_code("Systems Programming"), 2);
  EXPECT_EQ(t.branch_code("javascript"), 0);
  EXPECT_EQ(t.branch_code("c++"), 3);
  EXPECT_FALSE(t.category_code("Engineering").has_value());
  EXPECT_FALSE(t.empty());
  EXPECT_TRUE(domain::Taxonomy{}.empty());
  EXPECT_EQ(domain::to_json(t), json::parse(R"({
    "categories": ["Web Development", "Databases & Data", "Systems Programming"],
    "branches": ["javascript", "php", "sql", "c++"],
    "branch_categories": {"javascript": "Web Development", "php": "Web Development",
                          "sql": "Databases & Data", "c++": "Systems Programming"}})"));
}

TEST(Affinity, MemberConnectionStrengthUsesGraphcoreWithDisplayOrderCodes) {
  const auto t = test_taxonomy();
  const auto a = imported_profile(1, "aaa", "A", "Web Development", "javascript", 0.0, 1);
  const auto b = imported_profile(2, "bbb", "B", "Web Development", "javascript", 72.0, 1);
  const auto c = imported_profile(3, "ccc", "C", "Systems Programming", "c++", 100.0, 1);
  EXPECT_DOUBLE_EQ(domain::member_connection_strength(a, b, t), 0.784);  // 0.4 + 0.3 + 0.3 * 0.28
  EXPECT_DOUBLE_EQ(domain::member_connection_strength(a, c, t), 0.0);
  EXPECT_DOUBLE_EQ(domain::member_connection_strength(b, c, t), sg::graph::connection_strength({0, 0, 72.0}, {2, 3, 100.0}));
  const auto g = domain::to_graph_profile(c, t);
  EXPECT_EQ(g.category, 2);
  EXPECT_EQ(g.branch, 3);
  auto unknown = a;
  unknown.branch = "cobol";
  EXPECT_THROW((void)domain::member_connection_strength(unknown, b, t), std::invalid_argument);
}

TEST(UserRegistered, ValidPayloadIsNormalised) {
  const auto m = domain::parse_user_registered(registered());
  EXPECT_EQ(m.user_id, 100000001);
  EXPECT_EQ(m.handle, "ada_l");
  EXPECT_EQ(m.name, "Ada Lovelace");
  EXPECT_FALSE(m.location.has_value());
  EXPECT_EQ(m.about, "Line 1\nLine 2");
  json without_optionals = registered();
  without_optionals.erase("location");
  without_optionals.erase("about");
  EXPECT_NO_THROW((void)domain::parse_user_registered(without_optionals));
}

TEST(UserRegistered, InvalidPayloadsAreRejected) {
  std::vector<json> bad;
  bad.push_back(json::array());
  bad.push_back(registered(42));  // imported id space
  for (const char* key : {"user_id", "handle", "name", "category", "branch"}) {
    json missing = registered();
    missing.erase(key);
    bad.push_back(missing);
    json wrong_type = registered();
    wrong_type[key] = json::array();
    bad.push_back(wrong_type);
  }
  json bad_handle = registered();
  bad_handle["handle"] = "Ada L";
  bad.push_back(bad_handle);
  json long_name = registered();
  long_name["name"] = std::string(65, 'n');
  bad.push_back(long_name);
  json multiline_location = registered();
  multiline_location["location"] = "a\nb";
  bad.push_back(multiline_location);
  json long_about = registered();
  long_about["about"] = std::string(501, 'a');
  bad.push_back(long_about);
  json string_id = registered();
  string_id["user_id"] = "100000001";
  bad.push_back(string_id);
  for (const auto& payload : bad) {
    EXPECT_THROW((void)domain::parse_user_registered(payload), std::invalid_argument) << payload.dump();
  }
}

TEST(EventPayloads, OrderEndpointsAndRoundStrength) {
  EXPECT_EQ(domain::connection_added_payload(7, 9, 3, 0.70449),
            json::parse(R"({"graph_version":7,"user_a":3,"user_b":9,"strength":0.704})"));
  EXPECT_EQ(domain::connection_removed_payload(8, 9, 3), json::parse(R"({"graph_version":8,"user_a":3,"user_b":9})"));
  const auto p = imported_profile(3, "ccc", "C", "Systems Programming", "c++", 10.0, 5);
  const json created = domain::user_created_payload(9, p);
  EXPECT_EQ(created["graph_version"], 9);
  EXPECT_EQ(created["user"], domain::to_json(p));
}

TEST(Snapshot, JsonShape) {
  domain::Snapshot s;
  s.graph_version = 12;
  s.users.push_back(imported_profile(1, "aaa", "A", "Web Development", "javascript", 1.5, 10));
  s.users.push_back(imported_profile(2, "bbb", "B", "Web Development", "php", 2.5, 20));
  s.edges.push_back({1, 2, 0.3333});
  const json j = domain::to_json(s);
  EXPECT_EQ(j["graph_version"], 12);
  EXPECT_EQ(j["users"].size(), 2U);
  EXPECT_EQ(j["edges"], json::parse("[[1,2,0.333]]"));
}

TEST(UserQuery, SortParsingAndMatching) {
  EXPECT_EQ(domain::parse_user_sort("reputation"), domain::UserSort::reputation);
  EXPECT_EQ(domain::parse_user_sort("name"), domain::UserSort::name);
  EXPECT_EQ(domain::parse_user_sort("id"), domain::UserSort::id);
  EXPECT_FALSE(domain::parse_user_sort("Reputation").has_value());
  EXPECT_FALSE(domain::parse_user_sort("influence").has_value());

  const auto p = imported_profile(1, "jeff_atwood_1", "Jeff Atwood", "Web Development", "javascript", 97.03, 63708);
  domain::UserQuery q;
  EXPECT_TRUE(domain::matches(q, p));
  q.q = "ATWOOD";
  EXPECT_TRUE(domain::matches(q, p));
  q.q = "atwood_1";  // handle
  EXPECT_TRUE(domain::matches(q, p));
  q.q = "skeet";
  EXPECT_FALSE(domain::matches(q, p));
  q.q.reset();
  q.branch = "php";
  EXPECT_FALSE(domain::matches(q, p));
  q.branch.reset();
  q.min_reputation = 63709;
  EXPECT_FALSE(domain::matches(q, p));
  q.min_reputation = 63708;
  EXPECT_TRUE(domain::matches(q, p));
}

}  // namespace
