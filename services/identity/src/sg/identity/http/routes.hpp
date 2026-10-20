#pragma once

/// @file routes.hpp
/// HTTP routes of identity-service (contracts §4, §10.4). Routes only translate between HTTP and
/// the application layer; rules live in app/ and domain/.

#include <optional>
#include <string>

#include <sg/common/auth.hpp>
#include <sg/common/http.hpp>
#include <sg/common/rate_limit.hpp>

#include "sg/identity/app/account_service.hpp"
#include "sg/identity/config.hpp"

namespace sg::identity::http {

/// Limits for login/register (429 rate_limited + Retry-After): attempts per client address, and
/// failed logins per handle (successful logins are not counted, so a shared account such as the
/// demo member is only throttled while someone is guessing its password).
class AuthRateLimiters {
 public:
  explicit AuthRateLimiters(const RateLimitConfig& config);

  /// Counts one login attempt for the client address. @throws sg::common::ApiError 429 when over the limit.
  void check_login_client(const std::string& client);
  /// @throws sg::common::ApiError 429 while `handle` (normalised) has too many recent failed logins.
  void check_login_handle(const std::string& handle) const;
  /// Counts one failed login for `handle` (normalised; malformed handles are not tracked).
  void record_login_failure(const std::string& handle);
  /// Counts one registration for the client address. @throws sg::common::ApiError 429 when over the limit.
  void check_register(const std::string& client);

 private:
  sg::common::RateLimiter login_per_ip_;
  sg::common::RateLimiter login_failures_per_handle_;
  sg::common::RateLimiter register_per_ip_;
};

/// Registers
/// * `POST /api/v1/auth/register` → 201 `{token, expires_at, user: {id, handle, name}}`
/// * `POST /api/v1/auth/login`    → 200 same body
/// * `GET  /api/v1/auth/me`       → 200 `{id, handle, name, created_at}` (auth)
/// The references must outlive the server.
void register_routes(sg::common::HttpServer& server, app::AccountService& accounts,
                     const sg::common::TokenVerifier& verifier, AuthRateLimiters& limits);

}  // namespace sg::identity::http
