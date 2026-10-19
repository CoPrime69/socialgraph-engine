#include <atomic>
#include <chrono>
#include <future>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "sg/common/http.hpp"
#include "sg/common/log.hpp"

namespace {

using namespace sg::common;
using namespace std::chrono_literals;
using nlohmann::json;

class HttpServerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    set_log_sink([this](std::string_view line) {
      std::lock_guard lock(log_mutex_);
      log_lines_.emplace_back(line);
    });
    HttpServerOptions options;
    options.threads = 4;
    options.max_body_bytes = 1024;
    server_ = std::make_unique<HttpServer>("http-test", options);

    server_->get("/api/v1/ok", [](const httplib::Request&, httplib::Response& res) {
      write_json(res, 200, {{"ok", true}});
    });
    server_->post("/api/v1/echo", [](const httplib::Request& req, httplib::Response& res) {
      const json body = parse_json_body(req);
      write_json(res, 201, {{"name", body.at("name").get<std::string>()}});
    });
    server_->get("/api/v1/boom", [](const httplib::Request&, httplib::Response&) {
      throw std::runtime_error("secret internal detail: password=hunter2");
    });
    server_->get("/api/v1/conflict", [](const httplib::Request&, httplib::Response&) {
      throw validation_failed({{"handle", "taken"}});
    });
    server_->get("/api/v1/limited", [](const httplib::Request&, httplib::Response&) { throw rate_limited(42); });
    server_->get("/api/v1/items/:id", [](const httplib::Request& req, httplib::Response& res) {
      write_json(res, 200, {{"id", path_id(req, "id")}});
    });
    server_->get("/api/v1/page", [](const httplib::Request& req, httplib::Response& res) {
      write_json(res, 200, {{"limit", query_int(req, "limit", 20, 1, 100)}, {"q", query_param(req, "q").value_or("")}});
    });
    server_->get("/api/v1/whoami", [](const httplib::Request& req, httplib::Response& res) {
      write_json(res, 200, {{"ip", client_ip(req)}, {"request_id", current_request_id()}});
    });
    server_->get("/api/v1/slow", [this](const httplib::Request&, httplib::Response& res) {
      slow_entered_.set_value();
      std::this_thread::sleep_for(500ms);
      write_json(res, 200, {{"slow", true}});
    });
    server_->set_readiness_check([this] { return ready_.load(); });

    port_ = server_->bind_to_any_port();
    thread_ = std::thread([this] { server_->listen(); });
    ASSERT_TRUE(server_->wait_until_ready());
    client_ = std::make_unique<httplib::Client>("127.0.0.1", port_);
    client_->set_read_timeout(5, 0);
  }

  void TearDown() override {
    server_->stop();
    if (thread_.joinable()) {
      thread_.join();
    }
    set_log_sink({});
  }

  static json body(const httplib::Result& r) { return json::parse(r->body); }

  std::vector<std::string> logs() {
    std::lock_guard lock(log_mutex_);
    return log_lines_;
  }

  std::unique_ptr<HttpServer> server_;
  int port_ = 0;
  std::thread thread_;
  std::unique_ptr<httplib::Client> client_;
  std::atomic<bool> ready_{true};
  std::promise<void> slow_entered_;
  std::mutex log_mutex_;
  std::vector<std::string> log_lines_;
};

TEST_F(HttpServerTest, HealthAndReadiness) {
  auto r = client_->Get("/healthz");
  ASSERT_TRUE(r);
  EXPECT_EQ(r->status, 200);
  EXPECT_EQ(body(r), json({{"status", "ok"}}));

  r = client_->Get("/readyz");
  ASSERT_TRUE(r);
  EXPECT_EQ(r->status, 200);
  EXPECT_EQ(body(r), json({{"status", "ready"}}));

  ready_ = false;
  r = client_->Get("/readyz");
  ASSERT_TRUE(r);
  EXPECT_EQ(r->status, 503);
  EXPECT_EQ(body(r)["error"]["code"], "unavailable");

  server_->set_readiness_check([]() -> bool { throw std::runtime_error("db down"); });
  r = client_->Get("/readyz");
  ASSERT_TRUE(r);
  EXPECT_EQ(r->status, 503);
}

TEST_F(HttpServerTest, UnknownRouteIsJson404) {
  for (const auto& path : {"/nope", "/api/v1/nope", "/api/v1/ok/extra"}) {
    auto r = client_->Get(path);
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 404) << path;
    EXPECT_EQ(body(r)["error"]["code"], "not_found");
    EXPECT_EQ(r->get_header_value("Content-Type"), "application/json; charset=utf-8");
    EXPECT_FALSE(r->get_header_value("X-Request-Id").empty());
  }
  auto r = client_->Delete("/api/v1/ok");  // known path, unknown method
  ASSERT_TRUE(r);
  EXPECT_EQ(r->status, 404);
}

TEST_F(HttpServerTest, OversizedBodyIs413) {
  const std::string big = R"({"name":")" + std::string(2000, 'x') + R"("})";
  auto r = client_->Post("/api/v1/echo", big, "application/json");
  ASSERT_TRUE(r);
  EXPECT_EQ(r->status, 413);
  EXPECT_EQ(body(r)["error"]["code"], "payload_too_large");
}

TEST_F(HttpServerTest, MalformedJsonIs400) {
  for (const std::string& payload : {std::string("{\"name\":"), std::string("[1,2]"), std::string(""),
                                     std::string("\"str\""), std::string(100, '[') + std::string(100, ']')}) {
    auto r = client_->Post("/api/v1/echo", payload, "application/json");
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 400) << payload;
    EXPECT_EQ(body(r)["error"]["code"], "bad_request");
  }
  // Wrong type surfaces as a nlohmann::json exception inside the handler -> 400.
  auto r = client_->Post("/api/v1/echo", R"({"name": 5})", "application/json");
  ASSERT_TRUE(r);
  EXPECT_EQ(r->status, 400);
  r = client_->Post("/api/v1/echo", R"({"name": "ok"})", "application/json");
  ASSERT_TRUE(r);
  EXPECT_EQ(r->status, 201);
  EXPECT_EQ(body(r)["name"], "ok");
}

TEST_F(HttpServerTest, UnexpectedExceptionIs500WithoutLeakingDetails) {
  auto r = client_->Get("/api/v1/boom", {{"X-Request-Id", "req-boom-1"}});
  ASSERT_TRUE(r);
  EXPECT_EQ(r->status, 500);
  EXPECT_EQ(body(r), json::parse(R"({"error":{"code":"internal","message":"Internal server error"}})"));
  EXPECT_EQ(r->body.find("hunter2"), std::string::npos);
  for (const auto& [name, value] : r->headers) {
    EXPECT_EQ(value.find("hunter2"), std::string::npos) << name;
  }
  // ...but the error is logged with the request id.
  bool logged = false;
  for (const auto& line : logs()) {
    const json entry = json::parse(line);
    if (entry["msg"] == "unhandled exception" && entry["request_id"] == "req-boom-1") {
      logged = true;
    }
  }
  EXPECT_TRUE(logged);
}

TEST_F(HttpServerTest, ApiErrorsUseTheEnvelope) {
  auto r = client_->Get("/api/v1/conflict");
  ASSERT_TRUE(r);
  EXPECT_EQ(r->status, 422);
  EXPECT_EQ(body(r)["error"]["details"][0], json({{"field", "handle"}, {"message", "taken"}}));
  r = client_->Get("/api/v1/limited");
  ASSERT_TRUE(r);
  EXPECT_EQ(r->status, 429);
  EXPECT_EQ(r->get_header_value("Retry-After"), "42");
}

TEST_F(HttpServerTest, RequestIdIsEchoedOrGenerated) {
  auto r = client_->Get("/api/v1/whoami", {{"X-Request-Id", "abc-123"}});
  ASSERT_TRUE(r);
  EXPECT_EQ(r->get_header_value("X-Request-Id"), "abc-123");
  EXPECT_EQ(body(r)["request_id"], "abc-123");

  r = client_->Get("/api/v1/whoami");
  ASSERT_TRUE(r);
  const std::string generated = r->get_header_value("X-Request-Id");
  EXPECT_EQ(generated.size(), 32U);
  EXPECT_EQ(body(r)["request_id"], generated);

  r = client_->Get("/api/v1/whoami", {{"X-Request-Id", "bad id with spaces\"}"}});
  ASSERT_TRUE(r);
  EXPECT_EQ(r->get_header_value("X-Request-Id").size(), 32U);  // replaced, never reflected
}

TEST_F(HttpServerTest, SecurityHeaders) {
  auto r = client_->Get("/api/v1/ok");
  ASSERT_TRUE(r);
  EXPECT_EQ(r->get_header_value("X-Content-Type-Options"), "nosniff");
  EXPECT_EQ(r->get_header_value("Cache-Control"), "no-store");
  r = client_->Get("/healthz");
  ASSERT_TRUE(r);
  EXPECT_EQ(r->get_header_value("X-Content-Type-Options"), "nosniff");
  EXPECT_FALSE(r->has_header("Cache-Control"));
}

TEST_F(HttpServerTest, PathAndQueryParameters) {
  auto r = client_->Get("/api/v1/items/42");
  ASSERT_TRUE(r);
  EXPECT_EQ(r->status, 200);
  EXPECT_EQ(body(r)["id"], 42);
  for (const auto& path : {"/api/v1/items/abc", "/api/v1/items/0", "/api/v1/items/-1", "/api/v1/items/1.5",
                           "/api/v1/items/99999999999999999999", "/api/v1/items/%20"}) {
    r = client_->Get(path);
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 404) << path;
    EXPECT_EQ(body(r)["error"]["code"], "not_found");
  }
  r = client_->Get("/api/v1/page?limit=5&q=a%20b");
  ASSERT_TRUE(r);
  EXPECT_EQ(body(r), json({{"limit", 5}, {"q", "a b"}}));
  r = client_->Get("/api/v1/page");
  ASSERT_TRUE(r);
  EXPECT_EQ(body(r)["limit"], 20);
  for (const auto& path : {"/api/v1/page?limit=0", "/api/v1/page?limit=101", "/api/v1/page?limit=x",
                           "/api/v1/page?limit=5x"}) {
    r = client_->Get(path);
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 400) << path;
    EXPECT_EQ(body(r)["error"]["code"], "bad_request");
  }
}

TEST_F(HttpServerTest, ClientIpPrefersFirstForwardedHop) {
  auto r = client_->Get("/api/v1/whoami", {{"X-Forwarded-For", " 203.0.113.9 , 10.0.0.1"}});
  ASSERT_TRUE(r);
  EXPECT_EQ(body(r)["ip"], "203.0.113.9");
  r = client_->Get("/api/v1/whoami");
  ASSERT_TRUE(r);
  EXPECT_EQ(body(r)["ip"], "127.0.0.1");
}

TEST_F(HttpServerTest, AccessLogHasNoSecrets) {
  auto r = client_->Get("/api/v1/ok?token=querysecret", {{"Authorization", "Bearer supersecret"},
                                                          {"X-Request-Id", "log-check"}});
  ASSERT_TRUE(r);
  // The access log is written after the response has been sent, so wait for it.
  std::optional<json> access;
  const auto deadline = std::chrono::steady_clock::now() + 2s;
  while (!access && std::chrono::steady_clock::now() < deadline) {
    for (const auto& line : logs()) {
      const json entry = json::parse(line);
      if (entry["msg"] == "request" && entry["request_id"] == "log-check") {
        access = entry;
      }
    }
    std::this_thread::sleep_for(5ms);
  }
  ASSERT_TRUE(access.has_value());
  EXPECT_EQ((*access)["method"], "GET");
  EXPECT_EQ((*access)["path"], "/api/v1/ok");
  EXPECT_EQ((*access)["status"], 200);
  EXPECT_TRUE((*access)["duration_ms"].is_number());
  for (const auto& line : logs()) {
    EXPECT_EQ(line.find("supersecret"), std::string::npos) << line;
    EXPECT_EQ(line.find("querysecret"), std::string::npos) << line;
  }
}

TEST_F(HttpServerTest, GracefulStopFinishesInFlightRequest) {
  auto future = std::async(std::launch::async, [this] {
    httplib::Client client("127.0.0.1", port_);
    client.set_read_timeout(5, 0);
    return client.Get("/api/v1/slow");
  });
  slow_entered_.get_future().wait();
  const auto stop_started = std::chrono::steady_clock::now();
  server_->stop();
  thread_.join();  // listen() returns only after in-flight requests are done
  EXPECT_GE(std::chrono::steady_clock::now() - stop_started, 300ms);
  auto r = future.get();
  ASSERT_TRUE(r);
  EXPECT_EQ(r->status, 200);
  EXPECT_EQ(body(r)["slow"], true);
  // New connections are refused after stop.
  httplib::Client late("127.0.0.1", port_);
  late.set_connection_timeout(1, 0);
  EXPECT_FALSE(late.Get("/healthz"));
}

TEST(HttpServerLifecycle, StopBeforeListenReturnsImmediately) {
  HttpServer server("lifecycle", HttpServerOptions{});
  (void)server.bind_to_any_port();
  server.stop();
  const auto start = std::chrono::steady_clock::now();
  server.listen();
  EXPECT_LT(std::chrono::steady_clock::now() - start, 1s);
}

TEST(HttpServerLifecycle, ListenOnBusyPortThrows) {
  HttpServer first("first", HttpServerOptions{});
  const int port = first.bind_to_any_port();
  HttpServerOptions options;
  options.host = "127.0.0.1";
  options.port = port;
  HttpServer second("second", options);
  EXPECT_THROW(second.listen(), std::runtime_error);
}

}  // namespace
