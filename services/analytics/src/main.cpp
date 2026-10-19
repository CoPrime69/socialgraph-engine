#include "analytics_engine.hpp"
#include "api.hpp"
#include "event_consumer.hpp"
#include "read_model.hpp"

#include <sg/common/auth.hpp>
#include <sg/common/config.hpp>
#include <sg/common/http.hpp>
#include <sg/common/lifecycle.hpp>
#include <sg/common/log.hpp>

#include <httplib.h>

#include <iostream>
#include <string_view>
#include <thread>

namespace {

constexpr std::string_view kUsage = R"(analytics-service — read model and graph analytics (contracts §6)

Usage: analytics-service [serve|healthcheck|--help]

Environment:
  ANALYTICS_PORT                         listen port (default 8083)
  AUTH_PUBLIC_KEY                        identity Ed25519 public key, base64 (required)
  GRAPH_SERVICE_URL                      graph-service base URL (default http://localhost:8082)
  REDIS_HOST / REDIS_PORT                Redis for graph.events (default 127.0.0.1:6379)
  HTTP_THREADS                           HTTP event-loop threads (default 0 = one per CPU core)
  HTTP_WORKER_THREADS                    threads for requests that wait (?min_version, default 32)
  ANALYTICS_THREADS                      threads for centrality algorithms (default 0 = all cores)
  ANALYTICS_EXACT_BETWEENNESS_MAX_NODES  switch to sampled betweenness above this size (default 5000)
  ANALYTICS_BETWEENNESS_SAMPLES          pivots for sampled betweenness (default 256)
  ANALYTICS_MAX_WAIT_MS                  max wait for ?min_version (default 2000)
  ANALYTICS_EAGER_RECOMPUTE              recompute after each event batch (default true)
  ANALYTICS_MIN_REFRESH_MS               min time between background recomputations (default 1000)
  ANALYTICS_FULL_REFRESH_MS              min time between recomputations of PageRank, betweenness,
                                         closeness and communities; in between, refreshes carry them
                                         over (default 30000; 0 = always recompute everything)
  LOG_LEVEL                              debug|info|warn|error (default info)
)";

int port() { return static_cast<int>(sg::common::env_int("ANALYTICS_PORT", 8083, 1, 65535)); }

int healthcheck() {
  httplib::Client client("127.0.0.1", port());
  client.set_connection_timeout(2, 0);
  auto response = client.Get("/healthz");
  return response && response->status == 200 ? 0 : 1;
}

int serve() {
  using namespace sg::analytics;
  namespace common = sg::common;

  const common::TokenVerifier verifier(common::env_required("AUTH_PUBLIC_KEY"));

  EngineOptions engine_options;
  engine_options.threads = static_cast<unsigned>(common::env_int("ANALYTICS_THREADS", 0, 0, 1024));
  engine_options.exact_betweenness_max_nodes =
      static_cast<std::size_t>(common::env_int("ANALYTICS_EXACT_BETWEENNESS_MAX_NODES", 5000, 0, 100'000'000));
  engine_options.betweenness_samples =
      static_cast<std::size_t>(common::env_int("ANALYTICS_BETWEENNESS_SAMPLES", 256, 1, 1'000'000));
  engine_options.min_refresh_interval =
      std::chrono::milliseconds(common::env_int("ANALYTICS_MIN_REFRESH_MS", 1000, 0, 600'000));
  engine_options.full_refresh_interval =
      std::chrono::milliseconds(common::env_int("ANALYTICS_FULL_REFRESH_MS", 30'000, 0, 3'600'000));

  ReadModel model;
  AnalyticsEngine engine(model, engine_options);
  engine.start_background_refresh();

  common::HttpServerOptions http_options;
  http_options.port = port();
  http_options.threads = static_cast<std::size_t>(common::env_int("HTTP_THREADS", 0, 0, 512));
  // Handlers read immutable in-memory snapshots, so they run on the event loops; only requests that
  // may wait for a newer snapshot (?min_version) go to the worker pool.
  http_options.blocking_handlers = false;
  http_options.offload = may_wait;
  http_options.worker_threads =
      static_cast<std::size_t>(common::env_int("HTTP_WORKER_THREADS", 32, 1, 1024));
  common::HttpServer server("analytics-service", http_options);
  ApiOptions api_options;
  api_options.max_consistency_wait =
      std::chrono::milliseconds(common::env_int("ANALYTICS_MAX_WAIT_MS", 2000, 0, 30000));
  register_routes(server, model, engine, verifier, api_options);
  server.set_readiness_check([&model] { return model.ready(); });

  common::RedisOptions redis;
  redis.host = common::env_or("REDIS_HOST", "127.0.0.1");
  redis.port = static_cast<int>(common::env_int("REDIS_PORT", 6379, 1, 65535));
  EventConsumer::Options consumer_options;
  consumer_options.eager_recompute = common::env_bool("ANALYTICS_EAGER_RECOMPUTE", true);
  EventConsumer consumer(model, engine,
                         http_snapshot_fetcher(common::env_or("GRAPH_SERVICE_URL", "http://localhost:8082")),
                         redis, consumer_options);
  consumer.start();

  std::jthread http([&server] { server.listen(); });
  common::log_info("analytics-service started", {{"port", http_options.port}});
  const int signal = common::wait_for_shutdown_signal();
  common::log_info("shutting down", {{"signal", signal}});
  server.stop();
  consumer.stop();
  engine.stop();
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  sg::common::block_shutdown_signals();
  const std::string_view command = argc > 1 ? argv[1] : "serve";
  if (command == "--help" || command == "-h" || command == "help") {
    std::cout << kUsage;
    return 0;
  }
  if (command == "healthcheck") return healthcheck();
  if (command != "serve") {
    std::cerr << kUsage;
    return 2;
  }
  try {
    sg::common::init_logging("analytics-service",
                             sg::common::parse_log_level(sg::common::env_or("LOG_LEVEL", "info")));
    return serve();
  } catch (const std::exception& e) {
    sg::common::log_error("fatal error", {{"error", e.what()}});
    return 1;
  }
}
