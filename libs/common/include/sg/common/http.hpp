#pragma once

/// @file http.hpp
/// HTTP server shared by every service, served by Drogon's event loops (no thread per connection).
///
/// For every request the server:
/// * echoes a well-formed `X-Request-Id` or generates one, and returns it on the response;
/// * writes one access-log line `{method, path, status, duration_ms, request_id}` (never headers,
///   query strings or bodies), and builds nothing when that level is disabled;
/// * maps ApiError to its status and JSON envelope (+ `Retry-After`), nlohmann::json exceptions to
///   400 bad_request, an unreachable database to 503 unavailable and anything else to 500 internal
///   (logged with the request id, nothing leaked to the client);
/// * answers unknown routes with 404 not_found and bodies over `max_body_bytes` with 413
///   payload_too_large, both as JSON;
/// * adds `X-Content-Type-Options: nosniff`, and `Cache-Control: no-store` on /api and /internal.
///
/// `GET /healthz` (always 200 `{"status":"ok"}`) and `GET /readyz` (200 `{"status":"ready"}` or
/// 503 unavailable, from set_readiness_check()) are registered automatically.
///
/// Threading: one Drogon engine per process runs `threads` event loops; it starts with the first
/// listen() and listeners cannot be added once it runs. Handlers either run on a per-server worker
/// pool (`blocking_handlers`, the default, for handlers that wait on PostgreSQL or Redis) or inline
/// on the event loop (for purely in-memory handlers, which must never block).
///
/// Several HttpServer objects may exist in one process, one after another or at the same time (tests
/// do this): each owns its routes and serves the listener it was given. bind_to_any_port() reserves
/// an ephemeral loopback port; once the engine runs, it hands out one of a few spare ephemeral
/// listeners created at start-up instead. The engine shuts down when its last server stops, unless
/// it has such spare listeners (then it lives until the process exits).

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "sg/common/errors.hpp"

namespace sg::common {

/// Header fields in arrival order; names are compared case-insensitively.
using Headers = std::vector<std::pair<std::string, std::string>>;

/// Decoded query-string parameters in order (a name may repeat).
using Params = std::vector<std::pair<std::string, std::string>>;

/// An HTTP request as seen by a handler. Member names follow cpp-httplib's Request.
struct Request {
  std::string method;       ///< "GET", "POST", ... (HEAD is served as GET)
  std::string path;         ///< percent-decoded path without the query string
  std::string body;
  std::string remote_addr;  ///< socket peer address
  int remote_port = 0;
  Headers headers;
  Params params;  ///< query string
  std::unordered_map<std::string, std::string> path_params;  ///< `:name` segments of the matched route

  [[nodiscard]] bool has_header(std::string_view name) const;
  /// Value of the `index`-th header called `name` (case-insensitive), or "" when absent.
  [[nodiscard]] std::string get_header_value(std::string_view name, std::size_t index = 0) const;
  /// Appends a header (used to build requests in tests). Values containing CR/LF are ignored.
  void set_header(std::string name, std::string value);

  [[nodiscard]] bool has_param(std::string_view name) const;
  /// Value of the `index`-th query parameter called `name`, or "" when absent.
  [[nodiscard]] std::string get_param_value(std::string_view name, std::size_t index = 0) const;
};

/// The response a handler fills in. Member names follow cpp-httplib's Response.
struct Response {
  int status = -1;  ///< -1 until set; sent as 200
  std::string body;
  Headers headers;

  /// Appends a header. Names or values containing CR/LF (header injection) are ignored.
  void set_header(std::string name, std::string value);
  [[nodiscard]] bool has_header(std::string_view name) const;
  [[nodiscard]] std::string get_header_value(std::string_view name, std::size_t index = 0) const;
  /// Sets the body and replaces Content-Type.
  void set_content(std::string content, std::string_view content_type);
};

using Handler = std::function<void(const Request&, Response&)>;

struct HttpServerOptions {
  std::string host = "0.0.0.0";
  int port = 8080;
  /// Event-loop (I/O) threads of the process-wide engine; 0 = one per CPU core. The engine is
  /// configured by the first server that calls listen(); later servers share it as it is.
  std::size_t threads = 0;
  /// true: handlers run on a pool of `worker_threads` threads and may block (database, Redis).
  /// false: handlers run inline on the I/O thread and must not block, except requests for which
  /// `offload` returns true, which still go to the pool. GET /healthz always runs inline.
  bool blocking_handlers = true;
  std::size_t worker_threads = 32;
  std::function<bool(const Request&)> offload;
  /// Larger bodies get 413 payload_too_large. The engine itself refuses bodies above
  /// max(max_body_bytes, 1 MiB) of the server that started it (bare 413, connection closed).
  std::size_t max_body_bytes = 64 * 1024;
  /// Keep-alive connections idle this long are closed (engine-wide, set by the first server). Longer
  /// than nginx's upstream keepalive_timeout (60 s), so the gateway always closes first and never
  /// reuses a connection the service is closing.
  std::chrono::seconds idle_timeout{75};
};

class HttpServer {
 public:
  HttpServer(std::string service_name, HttpServerOptions options);
  ~HttpServer();
  HttpServer(const HttpServer&) = delete;
  HttpServer& operator=(const HttpServer&) = delete;

  /// Route registration. Patterns are literal paths whose segments may be parameters, e.g.
  /// "/api/v1/users/:id" (read with path_id(req, "id")); the first registered match wins.
  /// Register all routes before listen().
  /// @throws std::logic_error after listen().
  void get(const std::string& pattern, Handler h);
  void post(const std::string& pattern, Handler h);
  void put(const std::string& pattern, Handler h);
  void del(const std::string& pattern, Handler h);

  /// Readiness probe used by GET /readyz; an exception counts as "not ready".
  void set_readiness_check(std::function<bool()> check);

  /// Reserves an ephemeral port on `host` (for tests) and returns it; listen() then serves on it.
  /// @throws std::runtime_error when no port can be reserved.
  int bind_to_any_port(const std::string& host = "127.0.0.1");

  /// Reserves options.host:options.port (unless bind_to_any_port() was called), starts the engine
  /// if needed and serves until stop(). Returns immediately if stop() was already called.
  /// @throws std::runtime_error when the address cannot be bound.
  void listen();

  /// Graceful shutdown: stops serving new requests (new connections are closed), lets in-flight
  /// requests finish; listen() returns once they are done. Safe to call from any thread except a
  /// handler of this server, more than once, and before listen().
  void stop();

  /// Waits until listen() is serving. False on timeout.
  [[nodiscard]] bool wait_until_ready(std::chrono::milliseconds timeout = std::chrono::seconds(5)) const;

  [[nodiscard]] const HttpServerOptions& options() const noexcept;
  [[nodiscard]] const std::string& service_name() const noexcept;

  struct Impl;

 private:
  std::unique_ptr<Impl> impl_;
};

/// Parses the request body as a JSON object (nesting depth <= 64).
/// @throws ApiError 400 bad_request when the body is empty, malformed or not an object.
[[nodiscard]] nlohmann::json parse_json_body(const Request& req);

/// Serialises `body` (invalid UTF-8 replaced) as `application/json; charset=utf-8`.
void write_json(Response& res, int status, const nlohmann::json& body);

/// Writes the JSON error envelope of `error` (+ Retry-After when set).
void write_error(Response& res, const ApiError& error);

/// Query-string parameter `name`, if present (possibly empty).
[[nodiscard]] std::optional<std::string> query_param(const Request& req, const std::string& name);

/// Integer query parameter in [min, max]; `fallback` when absent or empty.
/// @throws ApiError 400 bad_request when it is not an integer or out of range.
[[nodiscard]] long long query_int(const Request& req, const std::string& name, long long fallback, long long min,
                                  long long max);

/// Positive 64-bit integer path parameter (`:name` in the route pattern).
/// @throws ApiError 404 not_found for anything else (garbage, zero, negative, overflow).
[[nodiscard]] std::int64_t path_id(const Request& req, const std::string& name);

/// Client address for rate limiting: first hop of X-Forwarded-For when present (the gateway
/// overwrites it with the peer address), else the socket peer address.
[[nodiscard]] std::string client_ip(const Request& req);

/// Request id of the request being handled on the calling thread (empty outside a request).
[[nodiscard]] std::string current_request_id();

}  // namespace sg::common
