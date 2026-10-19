#include <string>
#include <thread>

#include <gtest/gtest.h>
#include <httplib.h>
#include <nlohmann/json.hpp>

#include <sg/common/auth.hpp>
#include <sg/common/http.hpp>

#include "fakes.hpp"
#include "sg/graph_service/app/graph_service.hpp"
#include "sg/graph_service/http/routes.hpp"

namespace {

using nlohmann::json;
using sg::graph_service::testing::InMemoryGraphRepository;

constexpr std::int64_t kAda = 100000001;

/// graph routes on a real HttpServer bound to an ephemeral port, backed by the in-memory repository.
class GraphApiTest : public ::testing::Test {
 protected:
  void SetUp() override {
    sg::graph_service::testing::populate(repo);
    sg::common::HttpServerOptions o;
    o.threads = 4;
    server = std::make_unique<sg::common::HttpServer>("graph-test", o);
    sg::graph_service::http::register_routes(*server, graph, verifier);
    port = server->bind_to_any_port();
    thread = std::thread([this] { server->listen(); });
    ASSERT_TRUE(server->wait_until_ready());
  }
  void TearDown() override {
    server->stop();
    thread.join();
  }

  httplib::Headers auth(std::int64_t user = kAda) const {
    return {{"Authorization", "Bearer " + issuer.issue(user, "ada_l", "Ada").token}};
  }
  httplib::Result get(const std::string& path, const httplib::Headers& headers) const {
    httplib::Client c("127.0.0.1", port);
    return c.Get(path, headers);
  }
  httplib::Result post(const std::string& path, const std::string& body, const httplib::Headers& headers) const {
    httplib::Client c("127.0.0.1", port);
    return c.Post(path, headers, body, "application/json");
  }
  httplib::Result del(const std::string& path, const httplib::Headers& headers) const {
    httplib::Client c("127.0.0.1", port);
    return c.Delete(path, headers);
  }

  static void expect_error(const httplib::Result& res, int status, const std::string& code) {
    ASSERT_TRUE(res) << httplib::to_string(res.error());
    EXPECT_EQ(res->status, status) << res->body;
    const json body = json::parse(res->body);
    EXPECT_EQ(body["error"]["code"], code) << res->body;
  }

  sg::common::KeyPair keys = sg::common::generate_keypair();
  sg::common::TokenIssuer issuer{keys.seed_b64, std::chrono::hours(1)};
  sg::common::TokenVerifier verifier{keys.public_key_b64};
  InMemoryGraphRepository repo;
  sg::graph_service::app::GraphService graph{repo};
  std::unique_ptr<sg::common::HttpServer> server;
  int port = 0;
  std::thread thread;
};

TEST_F(GraphApiTest, TaxonomyIsPublic) {
  const auto res = get("/api/v1/meta/taxonomy", {});
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 200);
  const json body = json::parse(res->body);
  EXPECT_EQ(body["categories"], json::parse(R"(["Web Development","Databases & Data","Systems Programming"])"));
  EXPECT_EQ(body["branches"][3], "c++");
  EXPECT_EQ(body["branch_categories"]["sql"], "Databases & Data");
}

TEST_F(GraphApiTest, EverythingElseUnderApiRequiresAToken) {
  for (const char* path : {"/api/v1/users", "/api/v1/users/1", "/api/v1/me/profile", "/api/v1/me/connections"}) {
    expect_error(get(path, {}), 401, "unauthenticated");
    expect_error(get(path, {{"Authorization", "Bearer nope"}}), 401, "unauthenticated");
  }
  expect_error(post("/api/v1/me/connections", R"({"user_id":1})", {}), 401, "unauthenticated");
  expect_error(del("/api/v1/me/connections/1", {}), 401, "unauthenticated");
  const auto other_keys = sg::common::generate_keypair();
  const sg::common::TokenIssuer forger(other_keys.seed_b64, std::chrono::hours(1));
  expect_error(get("/api/v1/me/profile", {{"Authorization", "Bearer " + forger.issue(kAda, "ada_l", "Ada").token}}),
               401, "unauthenticated");
}

TEST_F(GraphApiTest, ListUsersPaginatesFiltersAndSorts) {
  auto res = get("/api/v1/users", auth());
  ASSERT_TRUE(res);
  ASSERT_EQ(res->status, 200) << res->body;
  json body = json::parse(res->body);
  EXPECT_EQ(body["total"], 4);
  EXPECT_EQ(body["limit"], 20);
  EXPECT_EQ(body["offset"], 0);
  ASSERT_EQ(body["items"].size(), 4U);
  EXPECT_EQ(body["items"][0]["id"], 1);  // highest reputation first
  EXPECT_EQ(body["items"][0]["connection_count"], 1);
  EXPECT_EQ(body["items"][3]["id"], kAda);  // reputation 1

  body = json::parse(get("/api/v1/users?sort=name&limit=2&offset=1", auth())->body);
  EXPECT_EQ(body["total"], 4);
  ASSERT_EQ(body["items"].size(), 2U);
  EXPECT_EQ(body["items"][0]["name"], "Geoff Dalgas");  // Ada, [Geoff, Jarrod], Jeff
  EXPECT_EQ(body["items"][1]["name"], "Jarrod Dixon");

  body = json::parse(get("/api/v1/users?q=JARROD", auth())->body);
  EXPECT_EQ(body["total"], 1);
  body = json::parse(get("/api/v1/users?branch=sql&min_reputation=10000&sort=id", auth())->body);
  ASSERT_EQ(body["items"].size(), 1U);
  EXPECT_EQ(body["items"][0]["id"], 3);
  body = json::parse(get("/api/v1/users?category=Databases%20%26%20Data&sort=id", auth())->body);
  EXPECT_EQ(body["total"], 2);
}

TEST_F(GraphApiTest, BadQueryParametersAre400) {
  for (const char* query : {"limit=0", "limit=101", "limit=abc", "offset=-1", "sort=influence", "min_reputation=x",
                            "min_reputation=-5", "category=Engineering", "branch=cobol"}) {
    expect_error(get(std::string("/api/v1/users?") + query, auth()), 400, "bad_request");
  }
  expect_error(get("/api/v1/users?q=" + std::string(101, 'a'), auth()), 400, "bad_request");
  const auto ok = get("/api/v1/users?q=" + std::string(100, 'a'), auth());
  EXPECT_EQ(ok->status, 200);
}

TEST_F(GraphApiTest, UserByIdAndPathGarbage) {
  const auto res = get("/api/v1/users/3", auth());
  ASSERT_EQ(res->status, 200);
  const json body = json::parse(res->body);
  EXPECT_EQ(body["handle"], "jarrod_3");
  EXPECT_EQ(body["external_url"], "https://stackoverflow.com/users/3");
  EXPECT_EQ(body["connection_count"], 1);
  for (const char* path : {"/api/v1/users/999", "/api/v1/users/0", "/api/v1/users/-1", "/api/v1/users/abc",
                           "/api/v1/users/99999999999999999999", "/api/v1/users/1%27%20OR%201=1"}) {
    expect_error(get(path, auth()), 404, "not_found");
  }
}

TEST_F(GraphApiTest, MeProfileIs404WhileProvisioning) {
  const auto mine = get("/api/v1/me/profile", auth());
  ASSERT_EQ(mine->status, 200);
  EXPECT_EQ(json::parse(mine->body)["source"], "member");
  expect_error(get("/api/v1/me/profile", auth(100000002)), 404, "not_found");
}

TEST_F(GraphApiTest, ConnectListAndDisconnect) {
  auto res = post("/api/v1/me/connections", R"({"user_id":1})", auth());
  ASSERT_TRUE(res);
  ASSERT_EQ(res->status, 201) << res->body;
  json body = json::parse(res->body);
  EXPECT_EQ(body["user"]["id"], 1);
  EXPECT_EQ(body["strength"], 0.709);
  EXPECT_EQ(body["interactions"], 0);
  EXPECT_EQ(body["origin"], "member");
  EXPECT_TRUE(body["first_interaction_at"].is_null());
  EXPECT_EQ(body["graph_version"], 2);

  ASSERT_EQ(post("/api/v1/me/connections", R"({"user_id":3})", auth())->status, 201);
  body = json::parse(get("/api/v1/me/connections", auth())->body);
  EXPECT_EQ(body["graph_version"], 3);
  ASSERT_EQ(body["items"].size(), 2U);
  EXPECT_GE(body["items"][0]["strength"].get<double>(), body["items"][1]["strength"].get<double>());
  EXPECT_EQ(body["items"][0]["user"]["id"], 1);  // same category and branch: stronger

  expect_error(post("/api/v1/me/connections", R"({"user_id":1})", auth()), 409, "conflict");
  expect_error(post("/api/v1/me/connections", R"({"user_id":100000001})", auth()), 422, "validation_failed");
  expect_error(post("/api/v1/me/connections", R"({"user_id":999})", auth()), 404, "not_found");
  expect_error(post("/api/v1/me/connections", R"({"user_id":"1"})", auth()), 400, "bad_request");
  expect_error(post("/api/v1/me/connections", R"({"user_id":1.5})", auth()), 400, "bad_request");
  expect_error(post("/api/v1/me/connections", R"({"user_id":-4})", auth()), 422, "validation_failed");
  expect_error(post("/api/v1/me/connections", R"({"user_id":18446744073709551615})", auth()), 422, "validation_failed");
  expect_error(post("/api/v1/me/connections", R"({})", auth()), 422, "validation_failed");
  expect_error(post("/api/v1/me/connections", R"({"user_id":)", auth()), 400, "bad_request");
  expect_error(post("/api/v1/me/connections", R"([1])", auth()), 400, "bad_request");
  expect_error(post("/api/v1/me/connections", R"({"user_id":1})", auth(100000002)), 404, "not_found");

  res = del("/api/v1/me/connections/1", auth());
  ASSERT_EQ(res->status, 200) << res->body;
  EXPECT_EQ(json::parse(res->body), json::parse(R"({"graph_version":4})"));
  expect_error(del("/api/v1/me/connections/1", auth()), 404, "not_found");
  expect_error(del("/api/v1/me/connections/abc", auth()), 404, "not_found");
  EXPECT_EQ(repo.version(), 4);
}

TEST_F(GraphApiTest, SnapshotIsInternalAndComplete) {
  const auto res = get("/internal/v1/snapshot", {});
  ASSERT_TRUE(res);
  ASSERT_EQ(res->status, 200);
  EXPECT_EQ(res->get_header_value("Cache-Control"), "no-store");
  const json body = json::parse(res->body);
  EXPECT_EQ(body["graph_version"], 1);
  EXPECT_EQ(body["users"].size(), 4U);
  EXPECT_EQ(body["edges"], json::parse("[[1,2,0.255],[2,3,0.661]]"));
}

}  // namespace
