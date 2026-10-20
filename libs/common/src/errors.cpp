#include "sg/common/errors.hpp"

#include <utility>

namespace sg::common {

ApiError::ApiError(int status, std::string code, std::string message, std::vector<FieldError> details)
    : std::runtime_error(std::move(message)), status_(status), code_(std::move(code)), details_(std::move(details)) {}

nlohmann::json ApiError::to_json() const {
  nlohmann::json error = {{"code", code_}, {"message", what()}};
  if (!details_.empty()) {
    nlohmann::json details = nlohmann::json::array();
    for (const auto& detail : details_) {
      details.push_back({{"field", detail.field}, {"message", detail.message}});
    }
    error["details"] = std::move(details);
  }
  return {{"error", std::move(error)}};
}

ApiError& ApiError::with_retry_after(int seconds) {
  retry_after_ = seconds < 1 ? 1 : seconds;
  return *this;
}

ApiError bad_request(std::string message) { return {400, "bad_request", std::move(message)}; }

ApiError unauthenticated(std::string message) { return {401, "unauthenticated", std::move(message)}; }

ApiError invalid_credentials() { return {401, "invalid_credentials", "Invalid handle or password"}; }

ApiError not_found(std::string message) { return {404, "not_found", std::move(message)}; }

ApiError conflict(std::string message) { return {409, "conflict", std::move(message)}; }

ApiError payload_too_large(std::size_t max_bytes) {
  return {413, "payload_too_large", "Request body exceeds " + std::to_string(max_bytes) + " bytes"};
}

ApiError validation_failed(std::vector<FieldError> details) {
  return {422, "validation_failed", "Request validation failed", std::move(details)};
}

ApiError rate_limited(int retry_after_seconds) {
  ApiError error{429, "rate_limited", "Too many requests, retry later"};
  error.with_retry_after(retry_after_seconds);
  return error;
}

ApiError unavailable(std::string message) { return {503, "unavailable", std::move(message)}; }

ApiError internal_error() { return {500, "internal", "Internal server error"}; }

void Validator::require(bool ok, std::string field, std::string message) {
  if (!ok) {
    errors_.push_back({std::move(field), std::move(message)});
  }
}

void Validator::add(std::string field, std::string message) {
  errors_.push_back({std::move(field), std::move(message)});
}

void Validator::throw_if_failed() const {
  if (!errors_.empty()) {
    throw validation_failed(errors_);
  }
}

}  // namespace sg::common
