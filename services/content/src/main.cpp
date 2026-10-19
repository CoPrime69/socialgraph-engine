#include "api.hpp"
#include "postgres_repository.hpp"
#include "seed_loader.hpp"

#include <sg/common/auth.hpp>
#include <sg/common/config.hpp>
#include <sg/common/http.hpp>
#include <sg/common/lifecycle.hpp>
#include <sg/common/log.hpp>
#include <sg/common/pg.hpp>

#include <httplib.h>

#include <iostream>
#include <string_view>
#include <thread>

namespace sg::content {
std::span<const sg::common::Migration> embedded_migrations();
}

namespace {

constexpr std::int64_t kMigrationLockKey = 0x5347'4d49'4743;  // "SGMIGC"

constexpr std::string_view kUsage = R"(content-service — questions, answers, comments, badges and tags (contracts §10.6)

Usage: content-service [serve|migrate|seed|healthcheck|--help]

Environment:
  CONTENT_PORT            listen port (default 8084)
  CONTENT_DATABASE_URL    PostgreSQL connection URI (required for serve/migrate/seed)
  AUTH_PUBLIC_KEY         identity Ed25519 public key, base64 (required for serve)
  DB_POOL_SIZE            connections in the pool (default 8)
  HTTP_THREADS            HTTP event-loop threads (default 0 = one per CPU core)
  HTTP_WORKER_THREADS     threads running request handlers (default 32)
  SEED_ON_EMPTY           import SEED_DIR when the database is empty (default false)
  SEED_DIR                dataset directory (default data/stackoverflow)
  CONTENT_CACHE_ENTRIES   rendered responses kept in memory (default 50000, 0 disables)
  LOG_LEVEL               debug|info|warn|error (default info)
)";

int port() { return static_cast<int>(sg::common::env_int("CONTENT_PORT", 8084, 1, 65535)); }

sg::common::PgPool make_pool() {
  sg::common::PgPoolOptions options;
  options.conninfo = sg::common::env_required("CONTENT_DATABASE_URL");
  options.size = static_cast<std::size_t>(sg::common::env_int("DB_POOL_SIZE", 8, 1, 256));
  options.application_name = "content-service";
  return sg::common::PgPool(options);
}

void migrate(sg::common::PgPool& pool) {
  sg::common::run_migrations(pool, sg::content::embedded_migrations(), kMigrationLockKey);
}

void seed(sg::common::PgPool& pool) {
  const auto report = sg::content::load_seed_if_empty(pool, sg::common::env_or("SEED_DIR", "data/stackoverflow"));
  if (!report.loaded) {
    sg::common::log_info("content seed skipped: database is not empty");
    return;
  }
  sg::common::log_info("content seed imported", {{"authors", report.authors},
                                                 {"tags", report.tags},
                                                 {"questions", report.questions},
                                                 {"question_tags", report.question_tags},
                                                 {"answers", report.answers},
                                                 {"comments", report.comments},
                                                 {"badges", report.badges},
                                                 {"user_tags", report.user_tags},
                                                 {"seconds", report.seconds}});
}

int serve() {
  namespace common = sg::common;
  const common::TokenVerifier verifier(common::env_required("AUTH_PUBLIC_KEY"));
  auto pool = make_pool();
  migrate(pool);
  if (common::env_bool("SEED_ON_EMPTY", false)) seed(pool);

  sg::content::PostgresContentRepository repository(pool);
  common::HttpServerOptions options;
  options.port = port();
  options.threads = static_cast<std::size_t>(common::env_int("HTTP_THREADS", 0, 0, 512));
  // Cache misses query PostgreSQL, so handlers run on the worker pool.
  options.worker_threads = static_cast<std::size_t>(common::env_int("HTTP_WORKER_THREADS", 32, 1, 1024));
  common::HttpServer server("content-service", options);
  sg::content::ApiOptions api_options;
  api_options.cache_entries =
      static_cast<std::size_t>(common::env_int("CONTENT_CACHE_ENTRIES", 50'000, 0, 10'000'000));
  sg::content::register_routes(server, repository, verifier, api_options);
  server.set_readiness_check([&repository] { return repository.ping(); });

  std::jthread http([&server] { server.listen(); });
  common::log_info("content-service started", {{"port", options.port}});
  const int signal = common::wait_for_shutdown_signal();
  common::log_info("shutting down", {{"signal", signal}});
  server.stop();
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
  if (command == "healthcheck") {
    httplib::Client client("127.0.0.1", port());
    client.set_connection_timeout(2, 0);
    auto response = client.Get("/healthz");
    return response && response->status == 200 ? 0 : 1;
  }
  try {
    sg::common::init_logging("content-service",
                             sg::common::parse_log_level(sg::common::env_or("LOG_LEVEL", "info")));
    if (command == "serve") return serve();
    if (command == "migrate" || command == "seed") {
      auto pool = make_pool();
      migrate(pool);
      if (command == "seed") seed(pool);
      return 0;
    }
    std::cerr << kUsage;
    return 2;
  } catch (const std::exception& e) {
    sg::common::log_error("fatal error", {{"error", e.what()}});
    return 1;
  }
}
