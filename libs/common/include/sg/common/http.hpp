#pragma once

/// @file http.hpp
/// HTTP server bootstrap shared by every service (wraps httplib::Server).
///
/// For every request the server:
/// * echoes a well-formed `X-Request-Id` or generates one, and returns it on the response;
/// * writes one access-log line `{method, path, status, duration_ms, request_id}` (never headers,
///   query strings or bodies);
/// * maps ApiError to its status and JSON envelope (+ `Retry-After`), nlohmann::json exceptions to
///   400 bad_request, an unreachable database to 503 unavailable and anything else to 500 internal
///   (logged with the request id, nothing leaked to the client);
/// * answers unknown routes with 404 not_found and bodies over `max_body_bytes` with 413
///   payload_too_large, both as JSON;
/// * adds `X-Content-Type-Options: nosniff`, and `Cache-Control: no-store` on /api and /internal.
///
/// `GET /healthz` (always 200 `{"status":"ok"}`) and `GET /readyz` (200 `{"status":"ready"}` or
/// 503 unavailable, from set_readiness_check()) are registered automatically.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "sg/common/errors.hpp"

namespace sg::common {

struct HttpServerOptions {
  std::string host = "0.0.0.0";
  int port = 8080;
  std::size_t threads = 8;
  std::size_t max_body_bytes = 64 * 1024;
  std::chrono::seconds read_timeout{5};
  std::chrono::seconds write_timeout{5};
};

using Handler = std::function<void(const httplib::Request&, httplib::Response&)>;

class HttpServer {
 public:
  HttpServer(std::string service_name, HttpServerOptions options);
  ~HttpServer();
  HttpServer(const HttpServer&) = delete;
  HttpServer& operator=(const HttpServer&) = delete;

  /// Route registration. Patterns use httplib syntax, e.g. "/api/v1/users/:id"
  /// (read with path_id(req, "id")). Register all routes before listen().
  void get(const std::string& pattern, Handler h);
  void post(const std::string& pattern, Handler h);
  void put(const std::string& pattern, Handler h);
  void del(const std::string& pattern, Handler h);

  /// Readiness probe used by GET /readyz; an exception counts as "not ready".
  void set_readiness_check(std::function<bool()> check);

  /// Binds an ephemeral port on `host` (for tests) and returns it; listen() then serves on it.
  /// @throws std::runtime_error when binding fails.
  int bind_to_any_port(const std::string& host = "127.0.0.1");

  /// Binds options.host:options.port (unless bind_to_any_port() was called) and serves until
  /// stop(). Returns immediately if stop() was already called.
  /// @throws std::runtime_error when the address cannot be bound.
  void listen();

  /// Graceful shutdown: stops accepting connections, lets in-flight requests finish; listen()
  /// returns once all worker threads are done. Safe to call from any thread, more than once, and
  /// before listen().
  void stop();

  /// Waits until listen() is accepting connections. False on timeout.
  [[nodiscard]] bool wait_until_ready(std::chrono::milliseconds timeout = std::chrono::seconds(5)) const;

  [[nodiscard]] httplib::Server& raw() { return *server_; }
  [[nodiscard]] const HttpServerOptions& options() const noexcept { return options_; }
  [[nodiscard]] const std::string& service_name() const noexcept { return service_name_; }

 private:
  [[nodiscard]] httplib::Server::Handler wrap(Handler h) const;
  void install_hooks();

  std::string service_name_;
  HttpServerOptions options_;
  std::unique_ptr<httplib::Server> server_;

  mutable std::mutex readiness_mutex_;
  std::function<bool()> readiness_;

  std::mutex state_mutex_;
  bool bound_ = false;
  bool listening_ = false;
  bool stop_requested_ = false;
};

/// Parses the request body as a JSON object (nesting depth <= 64).
/// @throws ApiError 400 bad_request when the body is empty, malformed or not an object.
[[nodiscard]] nlohmann::json parse_json_body(const httplib::Request& req);

/// Serialises `body` (invalid UTF-8 replaced) as `application/json; charset=utf-8`.
void write_json(httplib::Response& res, int status, const nlohmann::json& body);

/// Writes the JSON error envelope of `error` (+ Retry-After when set).
void write_error(httplib::Response& res, const ApiError& error);

/// Query-string parameter `name`, if present (possibly empty).
[[nodiscard]] std::optional<std::string> query_param(const httplib::Request& req, const std::string& name);

/// Integer query parameter in [min, max]; `fallback` when absent or empty.
/// @throws ApiError 400 bad_request when it is not an integer or out of range.
[[nodiscard]] long long query_int(const httplib::Request& req, const std::string& name, long long fallback,
                                  long long min, long long max);

/// Positive 64-bit integer path parameter (`:name` in the route pattern).
/// @throws ApiError 404 not_found for anything else (garbage, zero, negative, overflow).
[[nodiscard]] std::int64_t path_id(const httplib::Request& req, const std::string& name);

/// Client address for rate limiting: first hop of X-Forwarded-For when present (the gateway
/// overwrites it with the peer address), else the socket peer address.
[[nodiscard]] std::string client_ip(const httplib::Request& req);

/// Request id of the request being handled on the calling thread (empty outside a request).
[[nodiscard]] std::string current_request_id();

}  // namespace sg::common
