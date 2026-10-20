#include "sg/common/http.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <charconv>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <future>
#include <mutex>
#include <random>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <utility>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>

#include <drogon/HttpAppFramework.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <trantor/net/EventLoop.h>
#include <trantor/utils/Logger.h>

#include "sg/common/pqxx.hpp"

#include "sg/common/log.hpp"

namespace sg::common {
namespace {

using Clock = std::chrono::steady_clock;

/// Request id of the request being handled. A request is handled start to finish on one thread
/// (the I/O thread, or the worker that runs its handler), so a thread_local is exact and lock-free.
struct RequestContext {
  bool active = false;
  std::string request_id;
};

thread_local RequestContext t_request;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

/// Thrown by require_worker(); deliberately not a std::exception, so generic handlers let it pass.
struct NeedsWorker {};
/// True while an inline handler runs on a server that can move it to its worker pool.
thread_local bool t_may_defer = false;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

struct DeferScope {
  DeferScope() noexcept { t_may_defer = true; }
  ~DeferScope() { t_may_defer = false; }
  DeferScope(const DeferScope&) = delete;
  DeferScope& operator=(const DeferScope&) = delete;
};

constexpr std::size_t kMaxRequestIdLength = 128;
constexpr int kMaxJsonDepth = 64;
constexpr std::string_view kJsonContentType = "application/json; charset=utf-8";
/// Hard body limit of the engine; per-server limits below it answer with a JSON 413.
constexpr std::size_t kEngineBodyCap = 1024 * 1024;
/// Ephemeral loopback listeners created at engine start for bind_to_any_port() calls made later.
constexpr int kSpareListeners = 8;

// ---------------------------------------------------------------------------------------------
// Small helpers

#if defined(__SANITIZE_THREAD__)
#define SG_HTTP_TSAN 1
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define SG_HTTP_TSAN 1
#endif
#endif

#ifdef SG_HTTP_TSAN
extern "C" void __tsan_acquire(void* addr);  // NOLINT(bugprone-reserved-identifier)
extern "C" void __tsan_release(void* addr);  // NOLINT(bugprone-reserved-identifier)

/// Synchronisation object of queue_in_loop() under TSan (one for all loops: the acquire must happen
/// before the task reads anything it captured).
char g_loop_handoff;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
#endif

/// Runs `task` on `loop`. trantor's task queue is lock-free and, coming from a distro package built
/// without -fsanitize=thread, invisible to ThreadSanitizer, which would then report everything a
/// task touches as racing with the thread that queued it. Under TSan the hand-off is published with
/// an explicit release/acquire (the queue's own nodes are covered by cmake/tsan.supp); in other
/// builds this is plain queueInLoop().
template <class Task>
void queue_in_loop(trantor::EventLoop* loop, Task&& task) {
#ifdef SG_HTTP_TSAN
  std::function<void()> wrapped = [task = std::forward<Task>(task)]() mutable {
    __tsan_acquire(&g_loop_handoff);
    task();
  };
  __tsan_release(&g_loop_handoff);  // after the task object is built, so its allocation is published too
  loop->queueInLoop(std::move(wrapped));
#else
  loop->queueInLoop(std::forward<Task>(task));
#endif
}

bool iequals(std::string_view a, std::string_view b) noexcept {
  if (a.size() != b.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
      return false;
    }
  }
  return true;
}

bool is_header_safe(std::string_view text) noexcept {
  return text.find_first_of(std::string_view("\r\n\0", 3)) == std::string_view::npos;
}

const std::string* find_header(const Headers& headers, std::string_view name, std::size_t index) noexcept {
  for (const auto& [key, value] : headers) {
    if (iequals(key, name)) {
      if (index == 0) {
        return &value;
      }
      --index;
    }
  }
  return nullptr;
}

bool is_valid_request_id(std::string_view id) {
  if (id.empty() || id.size() > kMaxRequestIdLength) {
    return false;
  }
  return std::all_of(id.begin(), id.end(), [](unsigned char c) {
    return std::isalnum(c) != 0 || c == '-' || c == '_' || c == '.' || c == ':';
  });
}

/// 32 hex characters from a per-thread generator seeded by the OS. Request ids only need to be
/// unique, not unpredictable, and this avoids a getrandom() call on every request.
std::string new_request_id() {
  thread_local std::mt19937_64 engine = [] {
    std::random_device device;
    std::seed_seq seed{device(), device(), device(), device(), device(), device(), device(), device()};
    return std::mt19937_64(seed);
  }();
  static constexpr std::array<char, 16> kHex{'0', '1', '2', '3', '4', '5', '6', '7',
                                             '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
  std::string id(32, '0');
  for (int half = 0; half < 2; ++half) {
    std::uint64_t bits = engine();
    for (int i = 0; i < 16; ++i) {
      id[static_cast<std::size_t>(half * 16 + i)] = kHex[bits & 0xFU];
      bits >>= 4U;
    }
  }
  return id;
}

int hex_value(char c) noexcept {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

/// Query-string decoding: %XX escapes and '+' as space (invalid escapes are kept verbatim).
std::string decode_query_component(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (std::size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    if (c == '+') {
      out.push_back(' ');
    } else if (c == '%' && i + 2 < text.size() && hex_value(text[i + 1]) >= 0 && hex_value(text[i + 2]) >= 0) {
      out.push_back(static_cast<char>(hex_value(text[i + 1]) * 16 + hex_value(text[i + 2])));
      i += 2;
    } else {
      out.push_back(c);
    }
  }
  return out;
}

void parse_query(std::string_view query, Params& params) {
  while (!query.empty()) {
    const auto amp = query.find('&');
    const std::string_view pair = query.substr(0, amp);
    query = amp == std::string_view::npos ? std::string_view() : query.substr(amp + 1);
    const auto eq = pair.find('=');
    const std::string_view key = pair.substr(0, eq);
    if (key.empty()) {
      continue;
    }
    const std::string_view value = eq == std::string_view::npos ? std::string_view() : pair.substr(eq + 1);
    params.emplace_back(decode_query_component(key), decode_query_component(value));
  }
}

bool starts_with(std::string_view text, std::string_view prefix) {
  return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

/// JSON error for a response that has an error status but no body.
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

// ---------------------------------------------------------------------------------------------
// Routing: per method, literal patterns in a hash map plus an ordered list of patterns with
// ":param" segments. The first registered matching route wins.

enum Method : std::size_t { kGet, kPost, kPut, kDelete, kMethodCount };

std::optional<Method> to_method(drogon::HttpMethod method) noexcept {
  switch (method) {
    case drogon::Get:
      return kGet;
    case drogon::Post:
      return kPost;
    case drogon::Put:
      return kPut;
    case drogon::Delete:
      return kDelete;
    default:
      return std::nullopt;
  }
}

struct StringHash {
  using is_transparent = void;
  std::size_t operator()(std::string_view text) const noexcept { return std::hash<std::string_view>{}(text); }
};

class Router {
 public:
  void add(Method method, const std::string& pattern, Handler handler) {
    if (pattern.empty() || pattern.front() != '/') {
      throw std::invalid_argument("route pattern must start with '/': " + pattern);
    }
    Table& table = tables_[method];
    const std::size_t order = next_order_++;
    if (pattern.find("/:") == std::string::npos) {
      table.exact.try_emplace(pattern, Exact{order, std::move(handler)});
      return;
    }
    ParamRoute route{order, {}, std::move(handler)};
    std::string_view rest(pattern);
    rest.remove_prefix(1);
    for (;;) {
      const auto slash = rest.find('/');
      const std::string_view segment = rest.substr(0, slash);
      if (!segment.empty() && segment.front() == ':') {
        route.segments.push_back({std::string(segment.substr(1)), true});
      } else {
        route.segments.push_back({std::string(segment), false});
      }
      if (slash == std::string_view::npos) {
        break;
      }
      rest.remove_prefix(slash + 1);
    }
    table.params.push_back(std::move(route));
  }

  /// Handler for `path`, filling `path_params`; nullptr when nothing matches.
  const Handler* match(Method method, std::string_view path,
                       std::unordered_map<std::string, std::string>& path_params) const {
    const Table& table = tables_[method];
    const Exact* exact = nullptr;
    if (const auto it = table.exact.find(path); it != table.exact.end()) {
      exact = &it->second;
    }
    for (const ParamRoute& route : table.params) {
      if (exact != nullptr && route.order > exact->order) {
        break;
      }
      if (matches(route, path, nullptr)) {
        matches(route, path, &path_params);
        return &route.handler;
      }
    }
    return exact != nullptr ? &exact->handler : nullptr;
  }

 private:
  struct Segment {
    std::string text;  ///< literal text, or the parameter name
    bool param;
  };
  struct ParamRoute {
    std::size_t order;
    std::vector<Segment> segments;
    Handler handler;
  };
  struct Exact {
    std::size_t order;
    Handler handler;
  };
  struct Table {
    std::unordered_map<std::string, Exact, StringHash, std::equal_to<>> exact;
    std::vector<ParamRoute> params;
  };

  static bool matches(const ParamRoute& route, std::string_view path,
                      std::unordered_map<std::string, std::string>* out) {
    if (path.empty() || path.front() != '/') {
      return false;
    }
    path.remove_prefix(1);
    for (std::size_t i = 0; i < route.segments.size(); ++i) {
      const auto slash = path.find('/');
      const bool last = i + 1 == route.segments.size();
      if (last != (slash == std::string_view::npos)) {
        return false;  // segment counts differ
      }
      const std::string_view segment = path.substr(0, slash);
      const Segment& expected = route.segments[i];
      if (expected.param) {
        if (out != nullptr) {
          (*out)[expected.text] = std::string(segment);
        }
      } else if (segment != expected.text) {
        return false;
      }
      if (!last) {
        path.remove_prefix(slash + 1);
      }
    }
    return true;
  }

  std::array<Table, kMethodCount> tables_;
  std::size_t next_order_ = 0;
};

// ---------------------------------------------------------------------------------------------
// Worker pool for handlers that may block. shutdown() runs every queued task before joining.

class WorkerPool {
 public:
  explicit WorkerPool(std::size_t threads) {
    threads_.reserve(threads);
    for (std::size_t i = 0; i < threads; ++i) {
      threads_.emplace_back([this] { run(); });
    }
  }
  ~WorkerPool() { shutdown(); }
  WorkerPool(const WorkerPool&) = delete;
  WorkerPool& operator=(const WorkerPool&) = delete;

  void submit(std::function<void()> task) {
    {
      std::lock_guard lock(mutex_);
      queue_.push_back(std::move(task));
    }
    cv_.notify_one();
  }

  void shutdown() {
    {
      std::lock_guard lock(mutex_);
      if (stopping_) {
        return;
      }
      stopping_ = true;
    }
    cv_.notify_all();
    for (auto& thread : threads_) {
      if (thread.joinable()) {
        thread.join();
      }
    }
  }

 private:
  void run() {
    pthread_setname_np(pthread_self(), "sg-http-worker");
    for (;;) {
      std::function<void()> task;
      {
        std::unique_lock lock(mutex_);
        cv_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
        if (queue_.empty()) {
          return;  // stopping and drained
        }
        task = std::move(queue_.front());
        queue_.pop_front();
      }
      task();
    }
  }

  std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<std::function<void()>> queue_;
  bool stopping_ = false;
  std::vector<std::thread> threads_;
};

// ---------------------------------------------------------------------------------------------
// Port reservations. Drogon binds every listener once per I/O thread with SO_REUSEPORT, and exits
// the process when a bind fails, so ports are checked and held here first: a socket bound with
// SO_REUSEPORT (never listening, so it receives no connections) keeps other processes off the port
// until Drogon's own sockets are bound next to it.

class Fd {
 public:
  Fd() = default;
  explicit Fd(int fd) : fd_(fd) {}
  ~Fd() { reset(); }
  Fd(Fd&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}
  Fd& operator=(Fd&& other) noexcept {
    if (this != &other) {
      reset();
      fd_ = std::exchange(other.fd_, -1);
    }
    return *this;
  }
  Fd(const Fd&) = delete;
  Fd& operator=(const Fd&) = delete;
  [[nodiscard]] int get() const noexcept { return fd_; }
  void reset() noexcept {
    if (fd_ >= 0) {
      ::close(fd_);
      fd_ = -1;
    }
  }

 private:
  int fd_ = -1;
};

struct SocketAddress {
  sockaddr_storage storage{};
  socklen_t length = 0;
  int family = AF_INET;
};

SocketAddress parse_address(const std::string& host, int port) {
  if (port < 0 || port > 65535) {
    throw std::runtime_error("invalid port " + std::to_string(port));
  }
  SocketAddress address;
  const auto net_port = htons(static_cast<std::uint16_t>(port));
  auto* v4 = reinterpret_cast<sockaddr_in*>(&address.storage);
  if (::inet_pton(AF_INET, host.c_str(), &v4->sin_addr) == 1) {
    v4->sin_family = AF_INET;
    v4->sin_port = net_port;
    address.length = sizeof(sockaddr_in);
    address.family = AF_INET;
    return address;
  }
  auto* v6 = reinterpret_cast<sockaddr_in6*>(&address.storage);
  if (::inet_pton(AF_INET6, host.c_str(), &v6->sin6_addr) == 1) {
    v6->sin6_family = AF_INET6;
    v6->sin6_port = net_port;
    address.length = sizeof(sockaddr_in6);
    address.family = AF_INET6;
    return address;
  }
  throw std::runtime_error("cannot listen on '" + host + "': not a numeric IPv4/IPv6 address");
}

Fd bound_socket(const SocketAddress& address, bool reuse_port) {
  Fd fd(::socket(address.family, SOCK_STREAM | SOCK_CLOEXEC, 0));
  if (fd.get() < 0) {
    return fd;
  }
  const int yes = 1;
  ::setsockopt(fd.get(), SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
  if (reuse_port) {
    ::setsockopt(fd.get(), SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));
  }
  if (::bind(fd.get(), reinterpret_cast<const sockaddr*>(&address.storage), address.length) != 0) {
    fd.reset();
  }
  return fd;
}

/// Holds host:port (port 0 = a free ephemeral port) and returns the reservation and the port.
/// A fixed port is first bound without SO_REUSEPORT, so a port another process listens on (even with
/// SO_REUSEPORT) is reported as busy instead of being shared.
std::pair<Fd, int> reserve_port(const std::string& host, int port) {
  SocketAddress address = parse_address(host, port);
  const std::string where = host + ":" + std::to_string(port);
  if (port != 0 && bound_socket(address, false).get() < 0) {
    throw std::runtime_error("cannot listen on " + where + ": " + std::strerror(errno));
  }
  Fd fd = bound_socket(address, true);
  if (fd.get() < 0) {
    throw std::runtime_error("cannot listen on " + where + ": " + std::strerror(errno));
  }
  sockaddr_storage bound{};
  socklen_t length = sizeof(bound);
  if (::getsockname(fd.get(), reinterpret_cast<sockaddr*>(&bound), &length) != 0) {
    throw std::runtime_error("cannot listen on " + where + ": getsockname failed");
  }
  const int actual = bound.ss_family == AF_INET6 ? ntohs(reinterpret_cast<sockaddr_in6*>(&bound)->sin6_port)
                                                 : ntohs(reinterpret_cast<sockaddr_in*>(&bound)->sin_port);
  return {std::move(fd), actual};
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// Request / Response

bool Request::has_header(std::string_view name) const { return find_header(headers, name, 0) != nullptr; }

std::string Request::get_header_value(std::string_view name, std::size_t index) const {
  const std::string* value = find_header(headers, name, index);
  return value != nullptr ? *value : std::string();
}

void Request::set_header(std::string name, std::string value) {
  if (is_header_safe(name) && is_header_safe(value)) {
    headers.emplace_back(std::move(name), std::move(value));
  }
}

bool Request::has_param(std::string_view name) const {
  return std::any_of(params.begin(), params.end(), [name](const auto& p) { return p.first == name; });
}

std::string Request::get_param_value(std::string_view name, std::size_t index) const {
  for (const auto& [key, value] : params) {
    if (key == name) {
      if (index == 0) {
        return value;
      }
      --index;
    }
  }
  return {};
}

void Response::set_header(std::string name, std::string value) {
  if (!name.empty() && is_header_safe(name) && is_header_safe(value)) {
    headers.emplace_back(std::move(name), std::move(value));
  }
}

bool Response::has_header(std::string_view name) const { return find_header(headers, name, 0) != nullptr; }

std::string Response::get_header_value(std::string_view name, std::size_t index) const {
  const std::string* value = find_header(headers, name, index);
  return value != nullptr ? *value : std::string();
}

void Response::set_content(std::string content, std::string_view content_type) {
  body = std::move(content);
  std::erase_if(headers, [](const auto& h) { return iequals(h.first, "Content-Type"); });
  set_header("Content-Type", std::string(content_type));
}

// ---------------------------------------------------------------------------------------------
// The process-wide engine

namespace {

/// One listener of the engine. Created before the engine starts and never destroyed while it runs.
struct Slot {
  std::string host;
  int port = 0;
  bool spare = false;  ///< ephemeral; any bind_to_any_port() on `host` may lease it once free
  Fd reservation;      ///< holds the port until Drogon has bound it
  HttpServer::Impl* owner = nullptr;  ///< leasing server (Engine::mutex_)
  bool retired = false;               ///< owner has stopped (Engine::mutex_)
  std::atomic<HttpServer::Impl*> active{nullptr};  ///< server answering requests (read by I/O threads)
};

void serve_request(HttpServer::Impl& server, const drogon::HttpRequestPtr& request,
                   drogon::AdviceCallback&& callback);

drogon::HttpResponsePtr shutting_down_response() {
  auto response = drogon::HttpResponse::newHttpResponse(drogon::k503ServiceUnavailable, drogon::CT_NONE);
  response->setContentTypeString(kJsonContentType);
  response->setBody(unavailable("Server is shutting down").to_json().dump());
  response->addHeader("X-Content-Type-Options", "nosniff");
  return response;
}

class Engine {
 public:
  static Engine& instance() {
    // Never destroyed: the drogon singletons it drives outlive any static destructor ordering we
    // could rely on. at_exit() stops the event loops instead.
    static Engine* engine = new Engine();
    return *engine;
  }

  /// Leases a listener on host:port (0 = ephemeral) to `server`.
  Slot* lease(HttpServer::Impl* server, const std::string& host, int port) {
    std::unique_lock lock(mutex_);
    cv_.wait(lock, [this] { return phase_ != Phase::starting && phase_ != Phase::stopping; });
    for (auto& slot : slots_) {
      if (port != 0 && slot->port == port) {
        if (slot->owner != nullptr || slot->host != host) {
          throw std::runtime_error("cannot listen on " + host + ":" + std::to_string(port) +
                                   ": address already in use");
        }
        slot->owner = server;
        slot->retired = false;
        return slot.get();
      }
    }
    if (phase_ == Phase::stopped) {
      throw std::runtime_error("the HTTP engine has shut down and cannot serve " + host + ":" + std::to_string(port));
    }
    if (phase_ == Phase::running) {
      if (port == 0) {
        for (auto& slot : slots_) {
          if (slot->spare && slot->owner == nullptr && slot->host == host) {
            slot->owner = server;
            slot->retired = false;
            return slot.get();
          }
        }
        throw std::runtime_error("no spare HTTP listener left on " + host +
                                 " (listeners are fixed once the engine runs)");
      }
      throw std::runtime_error("cannot listen on " + host + ":" + std::to_string(port) +
                               ": the HTTP engine is already running and listeners are fixed once it starts");
    }
    auto [fd, actual] = reserve_port(host, port);
    auto slot = std::make_unique<Slot>();
    slot->host = host;
    slot->port = actual;
    slot->spare = port == 0;
    slot->reservation = std::move(fd);
    slot->owner = server;
    slots_.push_back(std::move(slot));
    return slots_.back().get();
  }

  /// The server no longer uses `slot` (it is being destroyed).
  void release(Slot* slot) {
    {
      std::lock_guard lock(mutex_);
      slot->owner = nullptr;
      slot->retired = false;
      if (phase_ == Phase::idle && !slot->spare) {
        std::erase_if(slots_, [slot](const auto& s) { return s.get() == slot; });
      }
    }
    shutdown_if_idle();
  }

  /// The server owning `slot` has stopped serving.
  void retire(Slot* slot) {
    {
      std::lock_guard lock(mutex_);
      slot->retired = true;
    }
    shutdown_if_idle();
  }

  /// Starts the engine unless it runs, configured from the first caller's options.
  void start(const HttpServerOptions& options) {
    std::unique_lock lock(mutex_);
    cv_.wait(lock, [this] { return phase_ != Phase::starting; });
    if (phase_ == Phase::running) {
      return;
    }
    if (phase_ != Phase::idle) {
      throw std::runtime_error("the HTTP engine has shut down and cannot be restarted in this process");
    }
    phase_ = Phase::starting;
    try {
      add_spare_listeners();
      configure(options);
    } catch (...) {
      phase_ = Phase::idle;
      cv_.notify_all();
      throw;
    }
    lock.unlock();

    thread_ = std::thread([this] { run(); });

    lock.lock();
    cv_.wait(lock, [this] { return listening_ || phase_ == Phase::stopped; });
    if (phase_ == Phase::stopped) {
      throw std::runtime_error("the HTTP engine failed to start");
    }
    phase_ = Phase::running;
    for (auto& slot : slots_) {
      slot->reservation.reset();  // Drogon's sockets hold the ports now
    }
    cv_.notify_all();
    lock.unlock();
    static std::once_flag exit_hook;
    std::call_once(exit_hook, [] { std::atexit([] { Engine::instance().shutdown(); }); });
  }

  /// Routes requests on `slot` to `server`.
  void activate(Slot* slot, HttpServer::Impl* server) { slot->active.store(server, std::memory_order_seq_cst); }

  /// Stops routing requests on `slot` and waits until every I/O thread has finished the requests it
  /// was already handling (inline handlers included; pooled ones are drained by the server).
  void deactivate(Slot* slot) {
    slot->active.store(nullptr, std::memory_order_seq_cst);
    {
      std::lock_guard lock(mutex_);
      if (phase_ != Phase::running) {
        return;
      }
    }
    std::vector<std::future<void>> barriers;
    const std::size_t loops = drogon::app().getThreadNum();
    for (std::size_t i = 0; i < loops; ++i) {
      trantor::EventLoop* loop = drogon::app().getIOLoop(i);
      if (loop == nullptr || loop->isInLoopThread()) {
        continue;
      }
      auto done = std::make_shared<std::promise<void>>();
      barriers.push_back(done->get_future());
      queue_in_loop(loop, [done] { done->set_value(); });
    }
    for (auto& barrier : barriers) {
      if (barrier.wait_for(std::chrono::seconds(10)) != std::future_status::ready) {
        log_warn("http engine: an I/O loop did not respond while stopping a server");
      }
    }
  }

  /// Quits the engine (process exit).
  void shutdown() {
    {
      std::lock_guard lock(mutex_);
      if (phase_ != Phase::running) {
        return;
      }
      phase_ = Phase::stopping;
    }
    quit_and_join();
  }

 private:
  enum class Phase { idle, starting, running, stopping, stopped };

  Engine() = default;

  /// Quits once no server is left and no spare listeners exist (production: one server per process).
  void shutdown_if_idle() {
    {
      std::lock_guard lock(mutex_);
      if (phase_ != Phase::running) {
        return;
      }
      const bool idle = std::all_of(slots_.begin(), slots_.end(), [](const auto& slot) {
        return !slot->spare && (slot->owner == nullptr || slot->retired);
      });
      if (!idle) {
        return;
      }
      phase_ = Phase::stopping;
    }
    quit_and_join();
  }

  void quit_and_join() {
    drogon::app().quit();
    if (thread_.joinable() && thread_.get_id() != std::this_thread::get_id()) {
      thread_.join();
    }
    std::lock_guard lock(mutex_);
    phase_ = Phase::stopped;
    cv_.notify_all();
  }

  void add_spare_listeners() {
    std::vector<std::string> hosts;
    for (const auto& slot : slots_) {
      if (slot->spare && std::find(hosts.begin(), hosts.end(), slot->host) == hosts.end()) {
        hosts.push_back(slot->host);
      }
    }
    for (const auto& host : hosts) {
      for (int i = 0; i < kSpareListeners; ++i) {
        auto [fd, port] = reserve_port(host, 0);
        auto slot = std::make_unique<Slot>();
        slot->host = host;
        slot->port = port;
        slot->spare = true;
        slot->reservation = std::move(fd);
        slots_.push_back(std::move(slot));
      }
    }
  }

  void configure(const HttpServerOptions& options) {
    // Drogon logs through trantor; keep stdout pure JSON lines.
    trantor::Logger::setOutputFunction(
        [](const char* message, std::uint64_t length) {
          std::string_view line(message, static_cast<std::size_t>(length));
          while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
            line.remove_suffix(1);
          }
          log_warn("http engine", {{"detail", std::string(line)}});
        },
        [] {});

    auto& app = drogon::app();
    const std::size_t body_cap = std::max(options.max_body_bytes, kEngineBodyCap);
    std::error_code ec;
    std::filesystem::path upload_dir = std::filesystem::temp_directory_path(ec);
    if (ec || upload_dir.empty()) {
      upload_dir = "/tmp";
    }
    upload_dir /= "sg-http-engine";
    app.setLogLevel(trantor::Logger::kWarn)
        .setThreadNum(options.threads)
        .disableSigtermHandling()  // services own SIGTERM/SIGINT (block_shutdown_signals + sigwait)
        .enableServerHeader(false)
        .enableDateHeader(true)
        .enableGzip(false)  // the gateway compresses
        .enableBrotli(false)
        .setIdleConnectionTimeout(static_cast<std::size_t>(std::max<long long>(options.idle_timeout.count(), 1)))
        .setClientMaxBodySize(body_cap)
        .setClientMaxMemoryBodySize(body_cap)  // never spool request bodies to files
        .setUploadPath(upload_dir.string());  // Drogon creates its upload directories at start-up
    app.enableReusePort(true);  // skip Drogon's own bind probe (it exits on failure); reserve_port() checked

    // Connections to a port whose server has stopped (or not started yet) are closed at once.
    app.registerNewConnectionAdvice([this](const trantor::InetAddress&, const trantor::InetAddress& local) {
      const Slot* slot = find(local.toPort());
      return slot != nullptr && slot->active.load(std::memory_order_acquire) != nullptr;
    });
    // Every request is answered here; Drogon's routers, static files and sessions are never reached.
    app.registerPreRoutingAdvice([this](const drogon::HttpRequestPtr& request, drogon::AdviceCallback&& callback,
                                        drogon::AdviceChainCallback&&) {
      const Slot* slot = find(request->localAddr().toPort());
      HttpServer::Impl* server = slot != nullptr ? slot->active.load(std::memory_order_acquire) : nullptr;
      if (server == nullptr) {
        callback(shutting_down_response());
        return;
      }
      serve_request(*server, request, std::move(callback));
    });
    // Runs on the main loop right before the listeners start; the I/O loops start listening in
    // tasks queued from there, so a task queued on every I/O loop afterwards runs once they listen.
    app.registerBeginningAdvice([this] {
      queue_in_loop(drogon::app().getLoop(), [this] {
        const std::size_t loops = drogon::app().getThreadNum();
        auto remaining = std::make_shared<std::atomic<std::size_t>>(loops);
        for (std::size_t i = 0; i < loops; ++i) {
          queue_in_loop(drogon::app().getIOLoop(i), [this, remaining] {
            if (remaining->fetch_sub(1) == 1) {
              std::lock_guard lock(mutex_);
              listening_ = true;
              cv_.notify_all();
            }
          });
        }
      });
    });
    for (const auto& slot : slots_) {
      app.addListener(slot->host, static_cast<std::uint16_t>(slot->port));
    }
    lookup_.clear();
    for (const auto& slot : slots_) {
      lookup_.push_back(slot.get());
    }
  }

  void run() {
    try {
      drogon::app().run();
    } catch (const std::exception& e) {
      log_error("http engine failed", {{"error", e.what()}});
    }
    std::lock_guard lock(mutex_);
    phase_ = Phase::stopped;
    cv_.notify_all();
  }

  /// I/O threads only: lookup_ is fixed before the engine thread starts.
  [[nodiscard]] const Slot* find(std::uint16_t port) const noexcept {
    for (const Slot* slot : lookup_) {
      if (slot->port == port) {
        return slot;
      }
    }
    return nullptr;
  }

  std::mutex mutex_;
  std::condition_variable cv_;
  Phase phase_ = Phase::idle;
  bool listening_ = false;
  std::vector<std::unique_ptr<Slot>> slots_;
  std::vector<const Slot*> lookup_;
  std::thread thread_;
};

}  // namespace

// ---------------------------------------------------------------------------------------------
// HttpServer

struct HttpServer::Impl {
  enum class Phase { idle, starting, serving, stopping, stopped };

  Impl(std::string name, HttpServerOptions opts) : service_name(std::move(name)), options(std::move(opts)) {}

  std::string service_name;
  HttpServerOptions options;
  Router router;

  mutable std::mutex readiness_mutex;
  std::function<bool()> readiness;

  mutable std::mutex mutex;
  mutable std::condition_variable cv;
  Phase phase = Phase::idle;
  bool stop_requested = false;
  bool routes_frozen = false;
  Slot* slot = nullptr;
  std::unique_ptr<WorkerPool> pool;

  void add_route(Method method, const std::string& pattern, Handler handler) {
    {
      std::lock_guard lock(mutex);
      if (routes_frozen) {
        throw std::logic_error("routes must be registered before listen()");
      }
    }
    router.add(method, pattern, std::move(handler));
  }

  [[nodiscard]] bool offloaded(const Request& req) const {
    if (req.path == "/healthz") {
      return false;  // liveness never waits behind handlers blocked on a database
    }
    if (options.blocking_handlers) {
      return true;
    }
    if (!options.offload) {
      return false;
    }
    try {
      return options.offload(req);
    } catch (...) {
      return false;
    }
  }

  /// Routing, the handler and error mapping.
  void handle(std::optional<Method> method, Request& req, Response& res) const {
    try {
      if (req.body.size() > options.max_body_bytes) {
        throw payload_too_large(options.max_body_bytes);
      }
      const Handler* handler = method ? router.match(*method, req.path, req.path_params) : nullptr;
      if (handler == nullptr) {
        write_error(res, not_found("Resource not found"));
        return;
      }
      (*handler)(req, res);
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
    } catch (const NeedsWorker&) {
      throw;  // serve_request() runs the handler again on a worker
    } catch (...) {
      log_error("unhandled exception",
                {{"request_id", current_request_id()}, {"method", req.method}, {"path", req.path}, {"error", "unknown"}});
      write_error(res, internal_error());
    }
  }

  void finalize(const Request& req, Response& res) const {
    if (res.status == -1) {
      res.status = 200;
    }
    if (res.status >= 400 && res.body.empty()) {
      const ApiError error = error_for_status(res.status, options.max_body_bytes);
      res.set_content(error.to_json().dump(), kJsonContentType);
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
  }

  static drogon::HttpResponsePtr to_drogon(Response& res) {
    auto out = drogon::HttpResponse::newHttpResponse(static_cast<drogon::HttpStatusCode>(res.status), drogon::CT_NONE);
    for (auto& [name, value] : res.headers) {
      if (iequals(name, "Content-Type")) {
        out->setContentTypeString(value);
      } else if (iequals(name, "Content-Length") || iequals(name, "Connection") ||
                 iequals(name, "Transfer-Encoding") || iequals(name, "Date") || iequals(name, "Server")) {
        continue;  // managed by the engine
      } else {
        out->addHeader(name, std::move(value));
      }
    }
    if (!res.body.empty()) {
      out->setBody(std::move(res.body));
    }
    return out;
  }

  static void access_log(const Request& req, int status, Clock::time_point start) {
    const bool probe = req.path == "/healthz" || req.path == "/readyz";
    const LogLevel level = probe ? LogLevel::debug : (status >= 500 ? LogLevel::warn : LogLevel::info);
    if (!log_enabled(level)) {
      return;  // nothing is formatted at 100k+ requests/s when the line would be dropped
    }
    const double elapsed_ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    log(level, "request",
        {{"method", req.method},
         {"path", req.path},
         {"status", status},
         {"duration_ms", std::round(elapsed_ms * 1000.0) / 1000.0},
         {"request_id", t_request.request_id}});
  }

  /// Handles one request on the calling thread and passes the response to `respond` (called once).
  template <class Respond>
  void serve(std::optional<Method> method, Request& req, Clock::time_point start, Respond&& respond) const {
    bool answered = false;
    try {
      const std::string* incoming = find_header(req.headers, "X-Request-Id", 0);
      t_request.active = true;
      t_request.request_id = incoming != nullptr && is_valid_request_id(*incoming) ? *incoming : new_request_id();
      Response res;
      handle(method, req, res);
      finalize(req, res);
      const int status = res.status;
      answered = true;
      respond(to_drogon(res));
      access_log(req, status, start);
    } catch (const NeedsWorker&) {
      t_request.active = false;
      t_request.request_id.clear();
      throw;
    } catch (const std::exception& e) {
      log_error("request processing failed", {{"request_id", t_request.request_id}, {"error", e.what()}});
      if (!answered) {
        respond(drogon::HttpResponse::newHttpResponse(drogon::k500InternalServerError, drogon::CT_NONE));
      }
    }
    t_request.active = false;
    t_request.request_id.clear();
  }
};

namespace {

void serve_request(HttpServer::Impl& server, const drogon::HttpRequestPtr& request,
                   drogon::AdviceCallback&& callback) {
  const Clock::time_point start = Clock::now();
  const auto method = to_method(request->method());
  Request req;
  req.method = request->methodString();
  req.path = request->path();
  req.body = std::string(request->body());
  const trantor::InetAddress& peer = request->peerAddr();
  req.remote_addr = peer.toIp();
  req.remote_port = peer.toPort();
  const auto& headers = request->headers();
  req.headers.reserve(headers.size());
  for (const auto& [name, value] : headers) {
    req.headers.emplace_back(name, value);
  }
  parse_query(request->query(), req.params);

  if (!server.offloaded(req)) {
    if (!server.pool) {
      server.serve(method, req, start, callback);
      return;
    }
    try {
      const DeferScope defer;
      server.serve(method, req, start, callback);
      return;
    } catch (const NeedsWorker&) {
      // The handler needs to block: run it again on a worker, below.
    }
  }
  // The response goes back to the connection's own loop, which then writes it without another
  // hop; the worker keeps no reference to Drogon's objects once it has handed them over.
  trantor::EventLoop* loop = trantor::EventLoop::getEventLoopOfCurrentThread();
  server.pool->submit(
      [&server, m = method, req = std::move(req), start, loop, callback = std::move(callback)]() mutable {
        server.serve(m, req, start, [loop, &callback](drogon::HttpResponsePtr response) {
          queue_in_loop(loop, [callback = std::move(callback), response = std::move(response)] { callback(response); });
        });
      });
}

}  // namespace

HttpServer::HttpServer(std::string service_name, HttpServerOptions options)
    : impl_(std::make_unique<Impl>(std::move(service_name), std::move(options))) {
  if (impl_->options.worker_threads == 0) {
    impl_->options.worker_threads = 1;
  }
  get("/healthz", [](const Request&, Response& res) { write_json(res, 200, {{"status", "ok"}}); });
  get("/readyz", [impl = impl_.get()](const Request&, Response& res) {
    require_worker();  // readiness checks may query a database
    std::function<bool()> check;
    {
      std::lock_guard lock(impl->readiness_mutex);
      check = impl->readiness;
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

HttpServer::~HttpServer() {
  stop();
  Slot* slot = nullptr;
  {
    std::lock_guard lock(impl_->mutex);
    slot = std::exchange(impl_->slot, nullptr);
  }
  if (slot != nullptr) {
    Engine::instance().release(slot);
  }
}

void HttpServer::get(const std::string& pattern, Handler h) { impl_->add_route(kGet, pattern, std::move(h)); }
void HttpServer::post(const std::string& pattern, Handler h) { impl_->add_route(kPost, pattern, std::move(h)); }
void HttpServer::put(const std::string& pattern, Handler h) { impl_->add_route(kPut, pattern, std::move(h)); }
void HttpServer::del(const std::string& pattern, Handler h) { impl_->add_route(kDelete, pattern, std::move(h)); }

void HttpServer::set_readiness_check(std::function<bool()> check) {
  std::lock_guard lock(impl_->readiness_mutex);
  impl_->readiness = std::move(check);
}

int HttpServer::bind_to_any_port(const std::string& host) {
  std::lock_guard lock(impl_->mutex);
  if (impl_->slot != nullptr) {
    throw std::runtime_error("HttpServer is already bound");
  }
  impl_->slot = Engine::instance().lease(impl_.get(), host, 0);
  impl_->options.host = host;
  impl_->options.port = impl_->slot->port;
  return impl_->slot->port;
}

void HttpServer::listen() {
  Impl& s = *impl_;
  Slot* slot = nullptr;
  {
    std::lock_guard lock(s.mutex);
    if (s.stop_requested || s.phase != Impl::Phase::idle) {
      return;
    }
    s.phase = Impl::Phase::starting;
    s.routes_frozen = true;
    slot = s.slot;
  }
  try {
    if (slot == nullptr) {
      slot = Engine::instance().lease(&s, s.options.host, s.options.port);
      std::lock_guard lock(s.mutex);
      s.slot = slot;
    }
    Engine::instance().start(s.options);
    if (s.options.blocking_handlers || s.options.offload) {
      s.pool = std::make_unique<WorkerPool>(s.options.worker_threads);
    }
  } catch (...) {
    std::lock_guard lock(s.mutex);
    s.phase = Impl::Phase::stopped;
    s.cv.notify_all();
    throw;
  }
  {
    std::unique_lock lock(s.mutex);
    if (s.stop_requested) {
      s.phase = Impl::Phase::stopped;
      s.cv.notify_all();
      lock.unlock();
      if (s.pool) {
        s.pool->shutdown();
      }
      Engine::instance().retire(slot);
      return;
    }
    Engine::instance().activate(slot, &s);
    s.phase = Impl::Phase::serving;
    s.cv.notify_all();
  }
  log_info("http server listening",
           {{"host", s.options.host},
            {"port", slot->port},
            {"io_threads", drogon::app().getThreadNum()},
            {"worker_threads", s.pool ? s.options.worker_threads : 0},
            {"blocking_handlers", s.options.blocking_handlers}});
  {
    std::unique_lock lock(s.mutex);
    s.cv.wait(lock, [&s] { return s.phase == Impl::Phase::stopped; });
  }
  log_info("http server stopped");
}

void HttpServer::stop() {
  Impl& s = *impl_;
  std::unique_lock lock(s.mutex);
  s.stop_requested = true;
  // listen() may be starting the engine; wait until it serves or gives up.
  s.cv.wait(lock, [&s] { return s.phase != Impl::Phase::starting; });
  if (s.phase == Impl::Phase::stopping) {
    s.cv.wait(lock, [&s] { return s.phase == Impl::Phase::stopped; });
    return;
  }
  if (s.phase != Impl::Phase::serving) {
    return;
  }
  s.phase = Impl::Phase::stopping;
  Slot* slot = s.slot;
  lock.unlock();

  Engine::instance().deactivate(slot);  // no new requests; inline ones have finished
  if (s.pool) {
    s.pool->shutdown();  // queued and running handlers finish
  }
  Engine::instance().retire(slot);  // the last server of a production process quits the engine

  lock.lock();
  s.phase = Impl::Phase::stopped;
  s.cv.notify_all();
}

bool HttpServer::wait_until_ready(std::chrono::milliseconds timeout) const {
  std::unique_lock lock(impl_->mutex);
  return impl_->cv.wait_for(lock, timeout, [this] { return impl_->phase == Impl::Phase::serving; });
}

const HttpServerOptions& HttpServer::options() const noexcept { return impl_->options; }
const std::string& HttpServer::service_name() const noexcept { return impl_->service_name; }

// ---------------------------------------------------------------------------------------------
// Helpers

nlohmann::json parse_json_body(const Request& req) {
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

void write_json(Response& res, int status, const nlohmann::json& body) {
  res.status = status;
  res.set_content(body.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace), kJsonContentType);
}

void write_error(Response& res, const ApiError& error) {
  write_json(res, error.status(), error.to_json());
  if (const auto retry = error.retry_after()) {
    res.set_header("Retry-After", std::to_string(*retry));
  }
}

std::optional<std::string> query_param(const Request& req, const std::string& name) {
  if (!req.has_param(name)) {
    return std::nullopt;
  }
  return req.get_param_value(name);
}

long long query_int(const Request& req, const std::string& name, long long fallback, long long min, long long max) {
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

std::int64_t path_id(const Request& req, const std::string& name) {
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

std::string client_ip(const Request& req) {
  if (const std::string* forwarded = find_header(req.headers, "X-Forwarded-For", 0)) {
    std::string_view first(*forwarded);
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

bool is_write(const Request& req) { return req.method != "GET" && req.method != "HEAD"; }

void require_worker() {
  if (t_may_defer) {
    throw NeedsWorker{};
  }
}

std::string current_request_id() { return t_request.active ? t_request.request_id : std::string(); }

}  // namespace sg::common
