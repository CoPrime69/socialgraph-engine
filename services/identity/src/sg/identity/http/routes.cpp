#include "sg/identity/http/routes.hpp"

#include <sg/common/errors.hpp>
#include <sg/common/time.hpp>
#include <sg/common/validation.hpp>

namespace sg::identity::http {
namespace {

using nlohmann::json;
using sg::common::ApiError;

void enforce(sg::common::RateLimiter& limiter, const std::string& key) {
  if (const auto retry = limiter.check(key)) {
    throw sg::common::rate_limited(static_cast<int>(retry->count()));
  }
}

/// A string field: absent or null -> nullopt; any other non-string type -> 400 bad_request.
std::optional<std::string> string_field(const json& body, const char* field) {
  const auto it = body.find(field);
  if (it == body.end() || it->is_null()) {
    return std::nullopt;
  }
  if (!it->is_string()) {
    throw sg::common::bad_request(std::string("Field '") + field + "' must be a string");
  }
  return it->get<std::string>();
}

json auth_body(const app::AuthResult& result) {
  return {{"token", result.token},
          {"expires_at", sg::common::unix_to_rfc3339(result.expires_at)},
          {"user", {{"id", result.account.id}, {"handle", result.account.handle}, {"name", result.account.name}}}};
}

}  // namespace

AuthRateLimiters::AuthRateLimiters(const RateLimitConfig& config)
    : login_per_ip_(config.login_per_ip, config.window),
      login_failures_per_handle_(config.login_per_handle, config.window),
      register_per_ip_(config.register_per_ip, config.window) {}

void AuthRateLimiters::check_login_client(const std::string& client) { enforce(login_per_ip_, client); }

void AuthRateLimiters::check_login_handle(const std::string& handle) const {
  if (const auto retry = login_failures_per_handle_.retry_after(handle)) {
    throw sg::common::rate_limited(static_cast<int>(retry->count()));
  }
}

void AuthRateLimiters::record_login_failure(const std::string& handle) {
  // Malformed handles can never sign in and must not grow the limiter with arbitrary keys.
  if (sg::common::is_valid_handle(handle)) {
    (void)login_failures_per_handle_.check(handle);
  }
}

void AuthRateLimiters::check_register(const std::string& client) { enforce(register_per_ip_, client); }

void register_routes(sg::common::HttpServer& server, app::AccountService& accounts,
                     const sg::common::TokenVerifier& verifier, AuthRateLimiters& limits) {
  server.post("/api/v1/auth/register", [&accounts, &limits](const httplib::Request& req, httplib::Response& res) {
    limits.check_register(sg::common::client_ip(req));
    const json body = sg::common::parse_json_body(req);
    domain::RegistrationInput input;
    input.handle = string_field(body, "handle");
    input.name = string_field(body, "name");
    input.password = string_field(body, "password");
    input.category = string_field(body, "category");
    input.branch = string_field(body, "branch");
    input.location = string_field(body, "location");
    input.about = string_field(body, "about");
    // "influence" is ignored (contracts §10.4): members start with influence 0.
    sg::common::write_json(res, 201, auth_body(accounts.register_account(input)));
  });

  server.post("/api/v1/auth/login", [&accounts, &limits](const httplib::Request& req, httplib::Response& res) {
    const json body = sg::common::parse_json_body(req);
    const auto handle = string_field(body, "handle");
    const auto password = string_field(body, "password");
    sg::common::Validator v;
    v.require(handle.has_value(), "handle", "is required");
    v.require(password.has_value(), "password", "is required");
    v.throw_if_failed();
    limits.check_login_client(sg::common::client_ip(req));
    const std::string normalized = sg::common::normalize_handle(*handle);
    limits.check_login_handle(normalized);
    try {
      sg::common::write_json(res, 200, auth_body(accounts.login(*handle, *password)));
    } catch (const ApiError& e) {
      if (e.code() == "invalid_credentials") {
        limits.record_login_failure(normalized);
      }
      throw;
    }
  });

  server.get("/api/v1/auth/me", [&accounts, &verifier](const httplib::Request& req, httplib::Response& res) {
    const auto claims = sg::common::require_auth(req, verifier);
    const auto account = accounts.me(claims.user_id);
    sg::common::write_json(res, 200,
                           {{"id", account.id},
                            {"handle", account.handle},
                            {"name", account.name},
                            {"created_at", sg::common::unix_to_rfc3339(account.created_at)}});
  });
}

}  // namespace sg::identity::http
