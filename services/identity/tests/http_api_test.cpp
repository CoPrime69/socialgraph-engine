#include <memory>
#include <string>
#include <thread>

#include <gtest/gtest.h>
#include <httplib.h>
#include <nlohmann/json.hpp>

#include <sg/common/auth.hpp>
#include <sg/common/crypto.hpp>
#include <sg/common/http.hpp>

#include "fakes.hpp"
#include "sg/identity/app/account_service.hpp"
#include "sg/identity/http/routes.hpp"

namespace {

using nlohmann::json;
using sg::identity::RateLimitConfig;
using sg::identity::testing::InMemoryAccountRepository;
using sg::identity::testing::SwitchableTaxonomyProvider;

const json kAda = {{"handle", "ada_l"},          {"name", "Ada Lovelace"}, {"password", "correct horse"},
                   {"category", "Web Development"}, {"branch", "javascript"}, {"about", "First programmer"}};

RateLimitConfig no_limits() {
  RateLimitConfig c;
  c.login_per_ip = 0;
  c.login_per_handle = 0;
  c.register_per_ip = 0;
  return c;
}

/// identity routes on a real HttpServer bound to an ephemeral port, backed by in-memory fakes.
class IdentityApi {
 public:
  explicit IdentityApi(const RateLimitConfig& limits)
      : limits_(limits),
        service_(repo, taxonomy, issuer, sg::common::PasswordHashParams::fast_for_tests()),
        server_("identity-test", options()) {
    sg::identity::http::register_routes(server_, service_, verifier, limits_);
    port_ = server_.bind_to_any_port();
    thread_ = std::thread([this] { server_.listen(); });
    EXPECT_TRUE(server_.wait_until_ready());
  }
  ~IdentityApi() {
    server_.stop();
    thread_.join();
  }
  IdentityApi(const IdentityApi&) = delete;
  IdentityApi& operator=(const IdentityApi&) = delete;

  [[nodiscard]] httplib::Client client() const {
    httplib::Client c("127.0.0.1", port_);
    c.set_read_timeout(10, 0);
    return c;
  }
  httplib::Result post(const std::string& path, const json& body, const httplib::Headers& headers = {}) const {
    return client().Post(path, headers, body.dump(), "application/json");
  }
  httplib::Result get(const std::string& path, const httplib::Headers& headers = {}) const {
    return client().Get(path, headers);
  }

  sg::common::KeyPair keys = sg::common::generate_keypair();
  sg::common::TokenIssuer issuer{keys.seed_b64, std::chrono::hours(8)};
  sg::common::TokenVerifier verifier{keys.public_key_b64};
  InMemoryAccountRepository repo;
  SwitchableTaxonomyProvider taxonomy{sg::identity::testing::shared_test_taxonomy()};

 private:
  static sg::common::HttpServerOptions options() {
    sg::common::HttpServerOptions o;
    o.threads = 4;
    return o;
  }

  sg::identity::http::AuthRateLimiters limits_;
  sg::identity::app::AccountService service_;
  sg::common::HttpServer server_;
  int port_ = 0;
  std::thread thread_;
};

json body_of(const httplib::Result& res) { return json::parse(res->body); }

void expect_error(const httplib::Result& res, int status, const std::string& code) {
  ASSERT_TRUE(res) << httplib::to_string(res.error());
  EXPECT_EQ(res->status, status) << res->body;
  const json body = body_of(res);
  ASSERT_TRUE(body.contains("error")) << res->body;
  EXPECT_EQ(body["error"]["code"], code) << res->body;
  EXPECT_TRUE(body["error"]["message"].is_string());
  EXPECT_EQ(res->get_header_value("Content-Type"), "application/json; charset=utf-8");
}

class IdentityHttpTest : public ::testing::Test {
 protected:
  IdentityApi api{no_limits()};
};

TEST_F(IdentityHttpTest, RegisterReturns201WithTokenAndUser) {
  json request = kAda;
  request["influence"] = 99.5;  // ignored (contracts §10.4)
  const auto res = api.post("/api/v1/auth/register", request);
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 201) << res->body;
  EXPECT_EQ(res->get_header_value("Content-Type"), "application/json; charset=utf-8");
  EXPECT_FALSE(res->get_header_value("X-Request-Id").empty());
  const json body = body_of(res);
  EXPECT_EQ(body["user"], json::parse(R"({"id":100000001,"handle":"ada_l","name":"Ada Lovelace"})"));
  const auto claims = api.verifier.verify(body["token"].get<std::string>());
  EXPECT_EQ(claims.user_id, 100000001);
  EXPECT_EQ(body["expires_at"], sg::common::unix_to_rfc3339(claims.expires_at));
  EXPECT_TRUE(body["expires_at"].get<std::string>().ends_with("Z"));

  const auto events = api.repo.events();
  ASSERT_EQ(events.size(), 1U);
  EXPECT_EQ(events[0].payload["about"], "First programmer");
  EXPECT_FALSE(events[0].payload.contains("influence"));
}

TEST_F(IdentityHttpTest, DuplicateHandleIs409) {
  ASSERT_EQ(api.post("/api/v1/auth/register", kAda)->status, 201);
  json again = kAda;
  again["handle"] = "ADA_L";
  expect_error(api.post("/api/v1/auth/register", again), 409, "conflict");
}

TEST_F(IdentityHttpTest, InvalidValuesAre422WithDetails) {
  json bad = kAda;
  bad["handle"] = "no";
  bad["category"] = "Engineering";
  bad.erase("password");
  const auto res = api.post("/api/v1/auth/register", bad);
  expect_error(res, 422, "validation_failed");
  EXPECT_EQ(body_of(res)["error"]["details"], json::parse(R"([
      {"field":"handle","message":"must be 3-32 characters of a-z, 0-9 or _"},
      {"field":"password","message":"is required"},
      {"field":"category","message":"must be one of the categories of GET /api/v1/meta/taxonomy"}])"));
  EXPECT_EQ(api.repo.size(), 0U);
}

TEST_F(IdentityHttpTest, WrongTypesAndMalformedBodiesAre400) {
  json wrong_type = kAda;
  wrong_type["handle"] = 42;
  expect_error(api.post("/api/v1/auth/register", wrong_type), 400, "bad_request");
  json wrong_optional = kAda;
  wrong_optional["about"] = json::array();
  expect_error(api.post("/api/v1/auth/register", wrong_optional), 400, "bad_request");
  auto c = api.client();
  expect_error(c.Post("/api/v1/auth/register", "{\"handle\":", "application/json"), 400, "bad_request");
  expect_error(c.Post("/api/v1/auth/register", "[1,2,3]", "application/json"), 400, "bad_request");
  expect_error(c.Post("/api/v1/auth/register", "", "application/json"), 400, "bad_request");
  expect_error(c.Post("/api/v1/auth/login", "{\"handle\":\"ada_l\",\"password\":7}", "application/json"), 400,
               "bad_request");
  EXPECT_EQ(api.repo.size(), 0U);
}

TEST_F(IdentityHttpTest, NullOptionalFieldsAreAccepted) {
  json request = kAda;
  request["location"] = nullptr;
  request["about"] = nullptr;
  ASSERT_EQ(api.post("/api/v1/auth/register", request)->status, 201);
  EXPECT_TRUE(api.repo.events()[0].payload["location"].is_null());
}

TEST_F(IdentityHttpTest, OversizedBodyIs413) {
  json huge = kAda;
  huge["about"] = std::string(70 * 1024, 'a');
  expect_error(api.post("/api/v1/auth/register", huge), 413, "payload_too_large");
}

TEST_F(IdentityHttpTest, LoginReturnsTokenAndFailsUniformly) {
  ASSERT_EQ(api.post("/api/v1/auth/register", kAda)->status, 201);
  const auto ok = api.post("/api/v1/auth/login", {{"handle", "Ada_L"}, {"password", "correct horse"}});
  ASSERT_TRUE(ok);
  EXPECT_EQ(ok->status, 200) << ok->body;
  EXPECT_EQ(body_of(ok)["user"]["handle"], "ada_l");
  EXPECT_EQ(api.verifier.verify(body_of(ok)["token"].get<std::string>()).user_id, 100000001);

  const auto wrong = api.post("/api/v1/auth/login", {{"handle", "ada_l"}, {"password", "wrong horse"}});
  const auto unknown = api.post("/api/v1/auth/login", {{"handle", "nobody"}, {"password", "correct horse"}});
  expect_error(wrong, 401, "invalid_credentials");
  expect_error(unknown, 401, "invalid_credentials");
  EXPECT_EQ(wrong->body, unknown->body);

  const auto missing = api.post("/api/v1/auth/login", {{"handle", "ada_l"}});
  expect_error(missing, 422, "validation_failed");
}

TEST_F(IdentityHttpTest, MeRequiresAValidToken) {
  const auto reg = api.post("/api/v1/auth/register", kAda);
  const std::string token = body_of(reg)["token"];
  expect_error(api.get("/api/v1/auth/me"), 401, "unauthenticated");
  expect_error(api.get("/api/v1/auth/me", {{"Authorization", "Bearer " + token + "x"}}), 401, "unauthenticated");
  const auto other_keys = sg::common::generate_keypair();
  const sg::common::TokenIssuer forger(other_keys.seed_b64, std::chrono::hours(1));
  expect_error(api.get("/api/v1/auth/me", {{"Authorization", "Bearer " + forger.issue(100000001, "ada_l", "Ada").token}}),
               401, "unauthenticated");

  const auto me = api.get("/api/v1/auth/me", {{"Authorization", "Bearer " + token}});
  ASSERT_TRUE(me);
  EXPECT_EQ(me->status, 200) << me->body;
  const json body = body_of(me);
  EXPECT_EQ(body["id"], 100000001);
  EXPECT_EQ(body["handle"], "ada_l");
  EXPECT_EQ(body["name"], "Ada Lovelace");
  EXPECT_TRUE(body["created_at"].get<std::string>().ends_with("Z"));
  EXPECT_FALSE(body.contains("password_hash"));

  api.repo.remove(100000001);  // account deleted while the token is still valid
  expect_error(api.get("/api/v1/auth/me", {{"Authorization", "Bearer " + token}}), 401, "unauthenticated");
}

TEST_F(IdentityHttpTest, RegisterIs503UntilTheTaxonomyIsKnown) {
  api.taxonomy.set(nullptr);
  expect_error(api.post("/api/v1/auth/register", kAda), 503, "unavailable");
  api.taxonomy.set(sg::identity::testing::shared_test_taxonomy());
  EXPECT_EQ(api.post("/api/v1/auth/register", kAda)->status, 201);
}

TEST_F(IdentityHttpTest, UnknownRoutesAndMethodsAreJson404) {
  expect_error(api.get("/api/v1/auth/nothing"), 404, "not_found");
  expect_error(api.get("/api/v1/auth/register"), 404, "not_found");
  const auto res = api.get("/healthz", {{"X-Request-Id", "req-123"}});
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 200);
  EXPECT_EQ(res->get_header_value("X-Request-Id"), "req-123");
}

TEST(IdentityRateLimits, FailedLoginsAreLimitedPerHandle) {
  RateLimitConfig limits = no_limits();
  limits.login_per_handle = 2;
  IdentityApi api(limits);
  ASSERT_EQ(api.post("/api/v1/auth/register", kAda)->status, 201);
  const json good = {{"handle", "ada_l"}, {"password", "correct horse"}};
  const json bad = {{"handle", "ADA_L"}, {"password", "wrong horse"}};
  for (int i = 0; i < 4; ++i) {  // successful logins are not counted
    EXPECT_EQ(api.post("/api/v1/auth/login", good)->status, 200);
  }
  // Two failures, from different clients, then the handle is locked for the rest of the window.
  expect_error(api.post("/api/v1/auth/login", bad, {{"X-Forwarded-For", "10.0.0.1"}}), 401, "invalid_credentials");
  expect_error(api.post("/api/v1/auth/login", bad, {{"X-Forwarded-For", "10.0.0.2"}}), 401, "invalid_credentials");
  const auto limited = api.post("/api/v1/auth/login", good, {{"X-Forwarded-For", "10.0.0.3"}});
  expect_error(limited, 429, "rate_limited");
  const int retry_after = std::stoi(limited->get_header_value("Retry-After"));
  EXPECT_GE(retry_after, 1);
  EXPECT_LE(retry_after, 60);
  // Other handles are unaffected.
  expect_error(api.post("/api/v1/auth/login", {{"handle", "bob_b"}, {"password", "whatever!"}}), 401,
               "invalid_credentials");
}

TEST(IdentityRateLimits, LoginAttemptsAreLimitedPerClient) {
  RateLimitConfig limits = no_limits();
  limits.login_per_ip = 3;
  IdentityApi api(limits);
  for (const char* handle : {"bob_1", "bob_2"}) {
    expect_error(api.post("/api/v1/auth/login", {{"handle", handle}, {"password", "whatever!"}},
                          {{"X-Forwarded-For", "10.0.0.9"}}),
                 401, "invalid_credentials");
  }
  expect_error(api.post("/api/v1/auth/login", {{"handle", "bob_3"}, {"password", "whatever!"}},
                        {{"X-Forwarded-For", "10.0.0.9, 192.168.1.1"}}),  // first hop counts
               401, "invalid_credentials");
  expect_error(api.post("/api/v1/auth/login", {{"handle", "bob_4"}, {"password", "whatever!"}},
                        {{"X-Forwarded-For", "10.0.0.9"}}),
               429, "rate_limited");
  expect_error(api.post("/api/v1/auth/login", {{"handle", "bob_4"}, {"password", "whatever!"}},
                        {{"X-Forwarded-For", "10.0.0.10"}}),
               401, "invalid_credentials");
}

TEST(IdentityRateLimits, RegisterIsLimitedPerClient) {
  RateLimitConfig limits = no_limits();
  limits.register_per_ip = 2;
  IdentityApi api(limits);
  for (const char* handle : {"user_one", "user_two"}) {
    json request = kAda;
    request["handle"] = handle;
    EXPECT_EQ(api.post("/api/v1/auth/register", request)->status, 201);
  }
  json third = kAda;
  third["handle"] = "user_three";
  expect_error(api.post("/api/v1/auth/register", third), 429, "rate_limited");
  EXPECT_EQ(api.repo.size(), 2U);
  // Another client is not affected.
  EXPECT_EQ(api.post("/api/v1/auth/register", third, {{"X-Forwarded-For", "203.0.113.7"}})->status, 201);
}

}  // namespace
