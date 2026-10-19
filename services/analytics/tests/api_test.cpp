#include "api.hpp"
#include "fixtures.hpp"

#include <sg/common/auth.hpp>
#include <sg/common/http.hpp>

#include <gtest/gtest.h>
#include <httplib.h>

#include <memory>
#include <set>
#include <thread>

namespace sg::analytics {
namespace {

using namespace std::chrono_literals;
using nlohmann::json;

class AnalyticsApiTest : public ::testing::Test {
 protected:
  void SetUp() override {
    keys_ = sg::common::generate_keypair();
    issuer_ = std::make_unique<sg::common::TokenIssuer>(keys_.seed_b64, std::chrono::hours(1));
    verifier_ = std::make_unique<sg::common::TokenVerifier>(keys_.public_key_b64);
    testing::load_two_cliques(model_);
    engine_ = std::make_unique<AnalyticsEngine>(model_, EngineOptions{});
    sg::common::HttpServerOptions options;
    options.threads = 4;
    server_ = std::make_unique<sg::common::HttpServer>("analytics-test", options);
    ApiOptions api_options;
    api_options.max_consistency_wait = 100ms;
    register_routes(*server_, model_, *engine_, *verifier_, api_options);
    server_->set_readiness_check([this] { return model_.ready(); });
    port_ = server_->bind_to_any_port();
    thread_ = std::jthread([this] { server_->listen(); });
    ASSERT_TRUE(server_->wait_until_ready());
    client_ = std::make_unique<httplib::Client>("127.0.0.1", port_);
    token_ = issuer_->issue(1, "user1", "User 1").token;
  }

  void TearDown() override {
    server_->stop();
    thread_.join();
  }

  httplib::Result get(const std::string& path, bool auth = true) {
    httplib::Headers headers;
    if (auth) headers.emplace("Authorization", "Bearer " + token_);
    return client_->Get(path, headers);
  }

  static json body(const httplib::Result& r) { return json::parse(r->body); }

  sg::common::KeyPair keys_;
  std::unique_ptr<sg::common::TokenIssuer> issuer_;
  std::unique_ptr<sg::common::TokenVerifier> verifier_;
  ReadModel model_;
  std::unique_ptr<AnalyticsEngine> engine_;
  std::unique_ptr<sg::common::HttpServer> server_;
  int port_ = 0;
  std::jthread thread_;
  std::unique_ptr<httplib::Client> client_;
  std::string token_;
};

TEST_F(AnalyticsApiTest, RequiresAuthentication) {
  auto r = get("/api/v1/analytics/summary", false);
  ASSERT_TRUE(r);
  EXPECT_EQ(r->status, 401);
  EXPECT_EQ(body(r)["error"]["code"], "unauthenticated");

  httplib::Headers forged{{"Authorization", "Bearer " + token_ + "x"}};
  auto r2 = client_->Get("/api/v1/analytics/summary", forged);
  EXPECT_EQ(r2->status, 401);
}

TEST_F(AnalyticsApiTest, Summary) {
  auto r = get("/api/v1/analytics/summary");
  ASSERT_EQ(r->status, 200);
  auto b = body(r);
  EXPECT_EQ(b["graph_version"], 10);
  EXPECT_EQ(b["users"], 9);
  EXPECT_EQ(b["connections"], 13);
  EXPECT_EQ(b["components"], 2);
  EXPECT_EQ(b["largest_component"], 8);
  EXPECT_NEAR(b["average_degree"].get<double>(), 26.0 / 9.0, 1e-12);
  EXPECT_NEAR(b["density"].get<double>(), 26.0 / 72.0, 1e-12);
  EXPECT_GE(b["communities"].get<int>(), 2);
  EXPECT_EQ(r->get_header_value("X-Graph-Version"), "10");
}

TEST_F(AnalyticsApiTest, InfluencersValidateMetricAndLimit) {
  auto r = get("/api/v1/analytics/influencers?metric=betweenness&limit=2");
  ASSERT_EQ(r->status, 200);
  auto b = body(r);
  ASSERT_EQ(b["items"].size(), 2U);
  EXPECT_EQ(b["items"][0]["user"]["id"], 4);
  EXPECT_EQ(b["items"][0]["rank"], 1);
  EXPECT_EQ(b["metric"], "betweenness");

  EXPECT_EQ(get("/api/v1/analytics/influencers?metric=fame")->status, 400);
  EXPECT_EQ(get("/api/v1/analytics/influencers?limit=0")->status, 400);
  EXPECT_EQ(get("/api/v1/analytics/influencers?limit=abc")->status, 400);
  EXPECT_EQ(body(get("/api/v1/analytics/influencers"))["items"].size(), 9U);
}

TEST_F(AnalyticsApiTest, CommunitiesDescribeMembers) {
  auto b = body(get("/api/v1/analytics/communities"));
  ASSERT_GE(b["items"].size(), 3U);  // two cliques + isolated user
  EXPECT_EQ(b["items"][0]["size"], 4);
  std::set<std::string> dominant;
  for (const auto& c : b["items"]) dominant.insert(c["dominant_category"].get<std::string>());
  EXPECT_TRUE(dominant.contains("Engineering"));
  EXPECT_TRUE(dominant.contains("Finance"));
  auto limited = body(get("/api/v1/analytics/communities?member_limit=1"));
  EXPECT_EQ(limited["items"][0]["members"].size(), 1U);
  EXPECT_EQ(limited["items"][0]["size"], 4);
}

TEST_F(AnalyticsApiTest, GraphAndDotExport) {
  auto b = body(get("/api/v1/analytics/graph"));
  EXPECT_EQ(b["nodes"].size(), 9U);
  EXPECT_EQ(b["edges"].size(), 13U);
  auto dot = get("/api/v1/analytics/graph.dot");
  ASSERT_EQ(dot->status, 200);
  EXPECT_NE(dot->get_header_value("Content-Type").find("text/vnd.graphviz"), std::string::npos);
  EXPECT_NE(dot->body.find("u4 -- u5"), std::string::npos);
}

TEST_F(AnalyticsApiTest, RecommendationsExcludeSelfAndNeighbours) {
  auto b = body(get("/api/v1/me/recommendations?limit=50"));
  std::set<std::int64_t> ids;
  for (const auto& item : b["items"]) ids.insert(item["user"]["id"].get<std::int64_t>());
  for (std::int64_t excluded : {1, 2, 3, 4}) EXPECT_FALSE(ids.contains(excluded));
  ASSERT_FALSE(b["items"].empty());
  EXPECT_EQ(b["items"][0]["user"]["id"], 5);  // the only friend-of-friend
  EXPECT_EQ(b["items"][0]["mutual_connections"], 1);
  EXPECT_TRUE(b["items"][0]["reasons"].contains("adamic_adar"));
  EXPECT_EQ(get("/api/v1/me/recommendations?limit=51")->status, 400);
}

TEST_F(AnalyticsApiTest, RecommendationsForUnprovisionedUserIs404) {
  token_ = issuer_->issue(777, "ghost", "Ghost").token;
  EXPECT_EQ(get("/api/v1/me/recommendations")->status, 404);
}

TEST_F(AnalyticsApiTest, Paths) {
  auto b = body(get("/api/v1/paths?to=8"));
  EXPECT_TRUE(b["found"]);
  EXPECT_EQ(b["degrees"], 3);  // 1-4-5-8
  ASSERT_EQ(b["path"].size(), 4U);
  EXPECT_EQ(b["path"][1]["id"], 4);

  auto none = body(get("/api/v1/paths?to=9"));
  EXPECT_FALSE(none["found"]);
  EXPECT_TRUE(none["degrees"].is_null());
  EXPECT_TRUE(none["path"].empty());

  auto self = body(get("/api/v1/paths?to=1"));
  EXPECT_EQ(self["degrees"], 0);

  EXPECT_EQ(body(get("/api/v1/paths?from=8&to=5"))["degrees"], 1);
  EXPECT_EQ(get("/api/v1/paths")->status, 400);
  EXPECT_EQ(get("/api/v1/paths?to=12345")->status, 404);
  EXPECT_EQ(get("/api/v1/paths?to=-3")->status, 400);
}

TEST_F(AnalyticsApiTest, UserMetrics) {
  auto b = body(get("/api/v1/users/4/metrics"));
  EXPECT_EQ(b["user"]["id"], 4);
  EXPECT_EQ(b["degree"], 4);
  EXPECT_EQ(b["ranks"]["betweenness"], 1);
  EXPECT_EQ(get("/api/v1/users/999/metrics")->status, 404);
  EXPECT_EQ(get("/api/v1/users/abc/metrics")->status, 404);
}

TEST_F(AnalyticsApiTest, MinVersionWaitsThenReportsStale) {
  auto stale = get("/api/v1/analytics/summary?min_version=11");
  ASSERT_EQ(stale->status, 200);
  EXPECT_EQ(stale->get_header_value("X-Graph-Stale"), "true");

  std::jthread writer([this] {
    std::this_thread::sleep_for(20ms);
    model_.apply(testing::connection_added(11, 9, 1));
  });
  // Give the writer most of the 100 ms budget.
  auto fresh = get("/api/v1/analytics/summary?min_version=11");
  ASSERT_EQ(fresh->status, 200);
  EXPECT_FALSE(fresh->has_header("X-Graph-Stale"));
  EXPECT_EQ(body(fresh)["graph_version"], 11);
  EXPECT_EQ(body(fresh)["connections"], 14);
}

TEST_F(AnalyticsApiTest, HealthAndReadiness) {
  EXPECT_EQ(client_->Get("/healthz")->status, 200);
  EXPECT_EQ(client_->Get("/readyz")->status, 200);
}

}  // namespace
}  // namespace sg::analytics
