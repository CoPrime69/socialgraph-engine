#pragma once

/// @file errors.hpp
/// API errors (contracts.md §3): an exception carrying the HTTP status, a stable machine-readable
/// code, a human-readable message and optional per-field details. HttpServer turns it into
/// `{"error":{"code","message","details":[{"field","message"}]}}`.

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace sg::common {

/// One invalid field of a request.
struct FieldError {
  std::string field;
  std::string message;

  friend bool operator==(const FieldError&, const FieldError&) = default;
};

/// Exception type for every expected failure of a request.
class ApiError : public std::runtime_error {
 public:
  ApiError(int status, std::string code, std::string message, std::vector<FieldError> details = {});

  [[nodiscard]] int status() const noexcept { return status_; }
  [[nodiscard]] const std::string& code() const noexcept { return code_; }
  [[nodiscard]] std::string message() const { return what(); }
  [[nodiscard]] const std::vector<FieldError>& details() const noexcept { return details_; }

  /// `{"error":{"code","message","details":[...]}}`; "details" is omitted when empty.
  [[nodiscard]] nlohmann::json to_json() const;

  /// Seconds for the Retry-After header (set for 429 responses).
  [[nodiscard]] std::optional<int> retry_after() const noexcept { return retry_after_; }
  ApiError& with_retry_after(int seconds);

 private:
  int status_;
  std::string code_;
  std::vector<FieldError> details_;
  std::optional<int> retry_after_;
};

/// 400 bad_request: malformed JSON, wrong types, bad query parameter.
[[nodiscard]] ApiError bad_request(std::string message);
/// 401 unauthenticated: missing, invalid or expired token.
[[nodiscard]] ApiError unauthenticated(std::string message = "Authentication required");
/// 401 invalid_credentials: failed login (identical for unknown handle and wrong password).
[[nodiscard]] ApiError invalid_credentials();
/// 404 not_found.
[[nodiscard]] ApiError not_found(std::string message);
/// 409 conflict.
[[nodiscard]] ApiError conflict(std::string message);
/// 413 payload_too_large.
[[nodiscard]] ApiError payload_too_large(std::size_t max_bytes);
/// 422 validation_failed with per-field details.
[[nodiscard]] ApiError validation_failed(std::vector<FieldError> details);
/// 429 rate_limited with Retry-After.
[[nodiscard]] ApiError rate_limited(int retry_after_seconds);
/// 503 unavailable: a dependency is down or not ready.
[[nodiscard]] ApiError unavailable(std::string message);
/// 500 internal: never carries internal details.
[[nodiscard]] ApiError internal_error();

/// Collects field errors and throws one 422 validation_failed listing all of them.
class Validator {
 public:
  /// Records `message` for `field` unless `ok`.
  void require(bool ok, std::string field, std::string message);
  /// Records an error unconditionally.
  void add(std::string field, std::string message);
  [[nodiscard]] bool ok() const noexcept { return errors_.empty(); }
  [[nodiscard]] const std::vector<FieldError>& errors() const noexcept { return errors_; }
  /// @throws ApiError 422 validation_failed when at least one error was recorded.
  void throw_if_failed() const;

 private:
  std::vector<FieldError> errors_;
};

}  // namespace sg::common
