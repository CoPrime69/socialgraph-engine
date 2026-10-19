#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <sg/common/http.hpp>

#include "sg/identity/infra/graph_taxonomy_client.hpp"

namespace {

using namespace std::chrono_literals;
using nlohmann::json;
using sg::identity::infra::GraphTaxonomyClient;
using sg::identity::infra::parse_taxonomy;

const json kTaxonomy = json::parse(R"({"categories":["Web Development","Databases"],
                                      "branches":["javascript","sql","php"],
                                      "branch_categories":{"javascript":"Web Development","sql":"Databases","php":"Web Development"}})");

/// A stand-in for graph-service's GET /api/v1/meta/taxonomy with a switchable answer.
class FakeGraphService {
 public:
  FakeGraphService() : server_("fake-graph", sg::common::HttpServerOptions{}) {
    server_.get("/api/v1/meta/taxonomy", [this](const httplib::Request&, httplib::Response& res) {
      requests_.fetch_add(1);
      std::lock_guard lock(mutex_);
      sg::common::write_json(res, status_, body_);
    });
    port_ = server_.bind_to_any_port();
    thread_ = std::thread([this] { server_.listen(); });
    EXPECT_TRUE(server_.wait_until_ready());
  }
  ~FakeGraphService() {
    server_.stop();
    thread_.join();
  }
  FakeGraphService(const FakeGraphService&) = delete;
  FakeGraphService& operator=(const FakeGraphService&) = delete;

  void answer(int status, json body) {
    std::lock_guard lock(mutex_);
    status_ = status;
    body_ = std::move(body);
  }
  [[nodiscard]] std::string url() const { return "http://127.0.0.1:" + std::to_string(port_); }
  [[nodiscard]] int requests() const { return requests_.load(); }

 private:
  sg::common::HttpServer server_;
  int port_ = 0;
  std::thread thread_;
  std::mutex mutex_;
  int status_ = 200;
  json body_ = kTaxonomy;
  std::atomic<int> requests_{0};
};

GraphTaxonomyClient::Options options_for(const std::string& url) {
  GraphTaxonomyClient::Options o;
  o.base_url = url;
  o.retry_interval = 20ms;
  o.refresh_interval = 50ms;
  o.connect_timeout = 1s;
  o.read_timeout = 1s;
  return o;
}

template <class Predicate>
bool eventually(Predicate p, std::chrono::milliseconds timeout = 5s) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (p()) {
      return true;
    }
    std::this_thread::sleep_for(10ms);
  }
  return p();
}

TEST(ParseTaxonomy, AcceptsTheContractShape) {
  const auto t = parse_taxonomy(kTaxonomy);
  EXPECT_EQ(t.categories, (std::vector<std::string>{"Web Development", "Databases"}));
  EXPECT_EQ(t.branches, (std::vector<std::string>{"javascript", "sql", "php"}));
  EXPECT_TRUE(t.has_branch("sql"));
  EXPECT_FALSE(t.has_branch("SQL"));
}

TEST(ParseTaxonomy, RejectsMalformedBodies) {
  for (const char* text : {R"([])", R"({"categories":["a"]})", R"({"categories":"a","branches":["b"]})",
                           R"({"categories":["a"],"branches":[1]})", R"({"categories":[""],"branches":["b"]})"}) {
    EXPECT_THROW((void)parse_taxonomy(json::parse(text)), std::invalid_argument) << text;
  }
}

TEST(GraphTaxonomyClient, RefreshOnceLoadsTheTaxonomy) {
  FakeGraphService graph;
  GraphTaxonomyClient client(options_for(graph.url() + "/"));  // a trailing slash is tolerated
  EXPECT_EQ(client.current(), nullptr);
  ASSERT_TRUE(client.refresh_once());
  const auto t = client.current();
  ASSERT_NE(t, nullptr);
  EXPECT_TRUE(t->has_category("Databases"));
  EXPECT_EQ(t->branches.size(), 3U);
}

TEST(GraphTaxonomyClient, FailedRefreshKeepsTheLastGoodCopy) {
  FakeGraphService graph;
  GraphTaxonomyClient client(options_for(graph.url()));
  ASSERT_TRUE(client.refresh_once());
  for (const auto& [status, body] : std::vector<std::pair<int, std::string>>{
           {503, R"({"error":{"code":"unavailable","message":"x"}})"},
           {200, R"("not an object")"},
           {200, R"({"categories":[],"branches":[]})"},
           {200, R"({"categories":["a"]})"}}) {
    graph.answer(status, json::parse(body));
    EXPECT_FALSE(client.refresh_once()) << body;
    ASSERT_NE(client.current(), nullptr);
    EXPECT_TRUE(client.current()->has_category("Web Development"));
  }
}

TEST(GraphTaxonomyClient, UnreachableServiceLeavesNoTaxonomy) {
  GraphTaxonomyClient client(options_for("http://127.0.0.1:1"));
  EXPECT_FALSE(client.refresh_once());
  EXPECT_EQ(client.current(), nullptr);
}

TEST(GraphTaxonomyClient, BackgroundLoaderRetriesUntilGraphIsReadyAndRefreshes) {
  FakeGraphService graph;
  graph.answer(503, json::parse(R"({"error":{"code":"unavailable","message":"starting"}})"));
  GraphTaxonomyClient client(options_for(graph.url()));
  client.start();
  ASSERT_TRUE(eventually([&] { return graph.requests() >= 3; }));
  EXPECT_EQ(client.current(), nullptr);

  graph.answer(200, kTaxonomy);
  ASSERT_TRUE(eventually([&] { return client.current() != nullptr; }));

  json changed = kTaxonomy;
  changed["branches"].push_back("python");
  graph.answer(200, changed);
  ASSERT_TRUE(eventually([&] { return client.current()->has_branch("python"); }));

  const auto started = std::chrono::steady_clock::now();
  client.stop();
  EXPECT_LT(std::chrono::steady_clock::now() - started, 2s);
  const int after_stop = graph.requests();
  std::this_thread::sleep_for(150ms);
  EXPECT_EQ(graph.requests(), after_stop);
}

}  // namespace
