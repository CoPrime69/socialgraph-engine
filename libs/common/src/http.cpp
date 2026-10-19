#include "sg/common/http.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <utility>

#include <sys/socket.h>

#include "sg/common/pqxx.hpp"

#include "sg/common/crypto.hpp"
#include "sg/common/log.hpp"

namespace sg::common {
namespace {

using Clock = std::chrono::steady_clock;

/// Per-request state. httplib handles every request of a connection on one worker thread, from the
/// pre-routing hook through the handler to the logger, so a thread_local is exact and lock-free.
struct RequestContext {
  bool active = false;
  Clock::time_point start;
  std::string request_id;
};

thread_local RequestContext t_request;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

constexpr std::size_t kMaxRequestIdLength = 128;
constexpr int kMaxJsonDepth = 64;
constexpr std::string_view kJsonContentType = "application/json; charset=utf-8";

bool is_valid_request_id(std::string_view id) {
  if (id.empty() || id.size() > kMaxRequestIdLength) {
    return false;
  }
  return std::all_of(id.begin(), id.end(), [](unsigned char c) {
    return std::isalnum(c) != 0 || c == '-' || c == '_' || c == '.' || c == ':';
  });
}

void begin_request(const httplib::Request& req) {
  t_request.active = true;
  t_request.start = Clock::now();
  const std::string incoming = req.get_header_value("X-Request-Id");
  t_request.request_id = is_valid_request_id(incoming) ? incoming : random_hex(16);
}

bool starts_with(std::string_view text, std::string_view prefix) {
  return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

/// JSON error for a response httplib produced itself (no route, body too large, parse failure).
ApiError error_for_status(int status, std::size_t max_body_bytes) {
  switch (status) {
    case 401:
      return unauthenticated();
    case 404:
      return not_found("Resource not found");
    case 413:
      return payload_too_large(max_body_bytes);
    case 503:
      return unavailable("Service unavailable");
    default:
      break;
  }
  if (status >= 500) {
    ApiError error = internal_error();
    return {status, error.code(), error.message()};
  }
  return {status, "bad_request", "Bad request"};
}

struct JsonTooDeep {};

}  // namespace

HttpServer::HttpServer(std::string service_name, HttpServerOptions options)
    : service_name_(std::move(service_name)), options_(std::move(options)), server_(std::make_unique<httplib::Server>()) {
  if (options_.threads == 0) {
    options_.threads = 1;
  }
  const std::size_t threads = options_.threads;
  server_->new_task_queue = [threads] { return new httplib::ThreadPool(threads); };
  server_->set_payload_max_length(options_.max_body_bytes);
  server_->set_read_timeout(options_.read_timeout);
  server_->set_write_timeout(options_.write_timeout);
  // httplib's default enables SO_REUSEPORT on Linux, which lets a second process bind the same port
  // and silently receive part of the traffic. SO_REUSEADDR alone still allows fast restarts.
  server_->set_socket_options([](socket_t sock) {
    int yes = 1;
    ::setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
  });
  // Headers and body are written separately; avoid Nagle + delayed-ACK stalls on small responses.
  server_->set_tcp_nodelay(true);
  install_hooks();

  get("/healthz", [](const httplib::Request&, httplib::Response& res) {
    write_json(res, 200, {{"status", "ok"}});
  });
  get("/readyz", [this](const httplib::Request&, httplib::Response& res) {
    std::function<bool()> check;
    {
      std::lock_guard lock(readiness_mutex_);
      check = readiness_;
    }
    bool ready = true;
    if (check) {
      try {
        ready = check();
      } catch (const std::exception& e) {
        log_warn("readiness check failed", {{"error", e.what()}});
        ready = false;
      }
    }
    if (ready) {
      write_json(res, 200, {{"status", "ready"}});
    } else {
      write_error(res, unavailable("Dependencies are not ready"));
    }
  });
}

HttpServer::~HttpServer() { stop(); }

void HttpServer::install_hooks() {
  server_->set_pre_routing_handler([](const httplib::Request& req, httplib::Response& res) {
    begin_request(req);
    res.set_header("X-Request-Id", t_request.request_id);
    return httplib::Server::HandlerResponse::Unhandled;
  });

  // Called for every response with status >= 400 just before it is written. Responses produced by
  // a handler already carry a JSON body; fill in the ones httplib generated itself.
  const std::size_t max_body = options_.max_body_bytes;
  server_->set_error_handler(
      httplib::Server::HandlerWithResponse([max_body](const httplib::Request&, httplib::Response& res) {
        if (!res.body.empty()) {
          return httplib::Server::HandlerResponse::Unhandled;
        }
        const ApiError error = error_for_status(res.status, max_body);
        res.set_content(error.to_json().dump(), std::string(kJsonContentType));
        return httplib::Server::HandlerResponse::Handled;
      }));

  // Last resort: handlers are wrapped and never throw, but keep httplib from putting exception
  // messages into response headers if something slips through.
  server_->set_exception_handler([](const httplib::Request& req, httplib::Response& res, std::exception_ptr ep) {
    std::string what = "unknown exception";
    try {
      if (ep) {
        std::rethrow_exception(ep);
      }
    } catch (const std::exception& e) {
      what = e.what();
    } catch (...) {
    }
    log_error("unhandled exception", {{"method", req.method}, {"path", req.path}, {"request_id", current_request_id()},
                                      {"error", what}});
    write_error(res, internal_error());
  });

  server_->set_post_routing_handler([](const httplib::Request& req, httplib::Response& res) {
    if (!t_request.active) {
      begin_request(req);  // request rejected before routing (e.g. malformed request line)
    }
    if (!res.has_header("X-Request-Id")) {
      res.set_header("X-Request-Id", t_request.request_id);
    }
    if (!res.has_header("X-Content-Type-Options")) {
      res.set_header("X-Content-Type-Options", "nosniff");
    }
    if ((starts_with(req.path, "/api/") || starts_with(req.path, "/internal/")) && !res.has_header("Cache-Control")) {
      res.set_header("Cache-Control", "no-store");
    }
  });

  server_->set_logger([](const httplib::Request& req, const httplib::Response& res) {
    const double elapsed_ms =
        t_request.active ? std::chrono::duration<double, std::milli>(Clock::now() - t_request.start).count() : 0.0;
    const bool probe = req.path == "/healthz" || req.path == "/readyz";
    const LogLevel level = probe ? LogLevel::debug : (res.status >= 500 ? LogLevel::warn : LogLevel::info);
    log(level, "request",
        {{"method", req.method},
         {"path", req.path},
         {"status", res.status},
         {"duration_ms", std::round(elapsed_ms * 1000.0) / 1000.0},
         {"request_id", t_request.active ? t_request.request_id : res.get_header_value("X-Request-Id")}});
    t_request.active = false;
    t_request.request_id.clear();
  });
}

httplib::Server::Handler HttpServer::wrap(Handler h) const {
  const std::size_t max_body = options_.max_body_bytes;
  return [max_body, h = std::move(h)](const httplib::Request& req, httplib::Response& res) {
    try {
      if (req.body.size() > max_body) {  // chunked bodies are not limited by httplib itself
        throw payload_too_large(max_body);
      }
      h(req, res);
    } catch (const ApiError& e) {
      if (e.status() >= 500) {
        log_warn("request failed", {{"request_id", current_request_id()}, {"status", e.status()}, {"error", e.what()}});
      }
      write_error(res, e);
    } catch (const nlohmann::json::exception& e) {
      log_debug("invalid JSON in request", {{"request_id", current_request_id()}, {"error", e.what()}});
      write_error(res, bad_request("Malformed JSON or a field of the wrong type"));
    } catch (const pqxx::broken_connection& e) {
      log_error("database unavailable", {{"request_id", current_request_id()}, {"error", e.what()}});
      write_error(res, unavailable("Database unavailable"));
    } catch (const std::exception& e) {
      log_error("unhandled exception",
                {{"request_id", current_request_id()}, {"method", req.method}, {"path", req.path}, {"error", e.what()}});
      write_error(res, internal_error());
    } catch (...) {
      log_error("unhandled exception",
                {{"request_id", current_request_id()}, {"method", req.method}, {"path", req.path}, {"error", "unknown"}});
      write_error(res, internal_error());
    }
  };
}

void HttpServer::get(const std::string& pattern, Handler h) { server_->Get(pattern, wrap(std::move(h))); }
void HttpServer::post(const std::string& pattern, Handler h) { server_->Post(pattern, wrap(std::move(h))); }
void HttpServer::put(const std::string& pattern, Handler h) { server_->Put(pattern, wrap(std::move(h))); }
void HttpServer::del(const std::string& pattern, Handler h) { server_->Delete(pattern, wrap(std::move(h))); }

void HttpServer::set_readiness_check(std::function<bool()> check) {
  std::lock_guard lock(readiness_mutex_);
  readiness_ = std::move(check);
}

int HttpServer::bind_to_any_port(const std::string& host) {
  std::lock_guard lock(state_mutex_);
  if (bound_) {
    throw std::runtime_error("HttpServer is already bound");
  }
  const int port = server_->bind_to_any_port(host);
  if (port < 0) {
    throw std::runtime_error("cannot bind an ephemeral port on " + host);
  }
  options_.host = host;
  options_.port = port;
  bound_ = true;
  return port;
}

void HttpServer::listen() {
  bool already_bound = false;
  {
    std::lock_guard lock(state_mutex_);
    if (stop_requested_) {
      return;
    }
    listening_ = true;
    already_bound = bound_;
  }
  bool bind_failed = false;
  if (!already_bound) {
    if (server_->bind_to_port(options_.host, options_.port)) {
      std::lock_guard lock(state_mutex_);
      bound_ = true;
    } else {
      bind_failed = true;
    }
  }
  if (!bind_failed) {
    log_info("http server listening", {{"host", options_.host}, {"port", options_.port}, {"threads", options_.threads}});
    server_->listen_after_bind();
  }
  {
    std::lock_guard lock(state_mutex_);
    listening_ = false;
  }
  if (bind_failed) {
    throw std::runtime_error("cannot listen on " + options_.host + ":" + std::to_string(options_.port));
  }
  log_info("http server stopped");
}

void HttpServer::stop() {
  {
    std::lock_guard lock(state_mutex_);
    stop_requested_ = true;
  }
  // listen() may be between "about to listen" and httplib's accept loop; httplib::Server::stop()
  // is a no-op until the loop runs, so wait for it to start (or for listen() to give up).
  for (;;) {
    {
      std::lock_guard lock(state_mutex_);
      if (!listening_ || server_->is_running()) {
        break;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  server_->stop();
}

bool HttpServer::wait_until_ready(std::chrono::milliseconds timeout) const {
  const auto deadline = Clock::now() + timeout;
  while (!server_->is_running()) {
    if (Clock::now() >= deadline) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return true;
}

nlohmann::json parse_json_body(const httplib::Request& req) {
  if (req.body.empty()) {
    throw bad_request("Request body must be a JSON object");
  }
  nlohmann::json body;
  try {
    body = nlohmann::json::parse(req.body, [](int depth, nlohmann::json::parse_event_t, nlohmann::json&) {
      if (depth > kMaxJsonDepth) {
        throw JsonTooDeep{};
      }
      return true;
    });
  } catch (const nlohmann::json::parse_error&) {
    throw bad_request("Malformed JSON in request body");
  } catch (const JsonTooDeep&) {
    throw bad_request("JSON nesting too deep");
  }
  if (!body.is_object()) {
    throw bad_request("Request body must be a JSON object");
  }
  return body;
}

void write_json(httplib::Response& res, int status, const nlohmann::json& body) {
  res.status = status;
  res.set_content(body.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace), std::string(kJsonContentType));
}

void write_error(httplib::Response& res, const ApiError& error) {
  write_json(res, error.status(), error.to_json());
  if (const auto retry = error.retry_after()) {
    res.set_header("Retry-After", std::to_string(*retry));
  }
}

std::optional<std::string> query_param(const httplib::Request& req, const std::string& name) {
  if (!req.has_param(name)) {
    return std::nullopt;
  }
  return req.get_param_value(name);
}

long long query_int(const httplib::Request& req, const std::string& name, long long fallback, long long min,
                    long long max) {
  const auto value = query_param(req, name);
  if (!value || value->empty()) {
    return fallback;
  }
  long long parsed = 0;
  const char* begin = value->data();
  const char* end = value->data() + value->size();
  const auto [ptr, ec] = std::from_chars(begin, end, parsed);
  if (ec != std::errc() || ptr != end || parsed < min || parsed > max) {
    throw bad_request("Query parameter '" + name + "' must be an integer between " + std::to_string(min) + " and " +
                      std::to_string(max));
  }
  return parsed;
}

std::int64_t path_id(const httplib::Request& req, const std::string& name) {
  const auto it = req.path_params.find(name);
  if (it == req.path_params.end()) {
    throw not_found("Resource not found");
  }
  const std::string& text = it->second;
  if (text.empty() || text.size() > 19 ||
      !std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) {
    throw not_found("Resource not found");
  }
  std::int64_t id = 0;
  const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), id);
  if (ec != std::errc() || ptr != text.data() + text.size() || id <= 0) {
    throw not_found("Resource not found");
  }
  return id;
}

std::string client_ip(const httplib::Request& req) {
  if (req.has_header("X-Forwarded-For")) {
    const std::string forwarded = req.get_header_value("X-Forwarded-For");
    std::string_view first(forwarded);
    first = first.substr(0, first.find(','));
    const auto begin = first.find_first_not_of(" \t");
    if (begin != std::string_view::npos) {
      first = first.substr(begin, first.find_last_not_of(" \t") - begin + 1);
      if (!first.empty() && first.size() <= 64) {
        return std::string(first);
      }
    }
  }
  return req.remote_addr;
}

std::string current_request_id() { return t_request.active ? t_request.request_id : std::string(); }

}  // namespace sg::common
