// identity-service: registration, password verification and Ed25519 access tokens.
// Subcommands: serve (default), migrate, seed, keygen, healthcheck, --help.

#include <atomic>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

#include <httplib.h>

#include <sg/common/auth.hpp>
#include <sg/common/config.hpp>
#include <sg/common/http.hpp>
#include <sg/common/lifecycle.hpp>
#include <sg/common/log.hpp>
#include <sg/common/outbox.hpp>
#include <sg/common/pg.hpp>

#include "sg/identity/app/account_service.hpp"
#include "sg/identity/config.hpp"
#include "sg/identity/http/routes.hpp"
#include "sg/identity/infra/demo_seed.hpp"
#include "sg/identity/infra/graph_taxonomy_client.hpp"
#include "sg/identity/infra/migrations.hpp"
#include "sg/identity/infra/postgres_account_repository.hpp"

namespace {

namespace common = sg::common;
using sg::identity::Config;

constexpr std::string_view kUsage = R"(identity-service: registration, login and Ed25519 access tokens (contracts §4, §10.4)

Usage: identity-service [serve|migrate|seed|keygen|healthcheck|--help]

  serve        migrate, seed the demo account when the database is empty (SEED_ON_EMPTY), then serve HTTP (default)
  migrate      apply database migrations and exit
  seed         migrate, create the demo account if the database is empty, and exit
  keygen       print a fresh AUTH_SIGNING_SEED / AUTH_PUBLIC_KEY pair and exit
  healthcheck  exit 0 when GET /healthz on 127.0.0.1:$IDENTITY_PORT answers 200

Environment:
  IDENTITY_PORT               listen port (default 8081)
  IDENTITY_DATABASE_URL       PostgreSQL connection URI (required for serve/migrate/seed)
  AUTH_SIGNING_SEED           Ed25519 32-byte seed, base64 (required for serve; secret)
  AUTH_TOKEN_TTL_SECONDS      access token lifetime (default 28800)
  PASSWORD_HASH_PROFILE       owasp (Argon2id m=19 MiB, t=2; default) | fast (tests only)
  REDIS_HOST / REDIS_PORT     event bus for identity.events (default 127.0.0.1:6379)
  GRAPH_SERVICE_URL           graph-service base URL for the taxonomy (default http://localhost:8082)
  TAXONOMY_REFRESH_SECONDS    taxonomy refresh period (default 300)
  SEED_ON_EMPTY               create the demo account when the database is empty (default true)
  SEED_DEMO_PASSWORD          password of the demo account (8-128 bytes; secret)
  HTTP_THREADS                request worker threads (default 8)
  DB_POOL_SIZE                PostgreSQL connections (default 8)
  RATE_LIMIT_WINDOW_SECONDS   rate limit window (default 60)
  LOGIN_RATE_LIMIT_PER_IP     login attempts per client and window (default 30, 0 = off)
  LOGIN_RATE_LIMIT_PER_HANDLE failed logins per handle and window (default 10, 0 = off)
  REGISTER_RATE_LIMIT_PER_IP  registrations per client and window (default 20, 0 = off)
  LOG_LEVEL                   debug|info|warn|error (default info)
)";

common::PgPool make_pool(const Config& config) {
  common::PgPoolOptions options;
  options.conninfo = config.require_database_url();
  options.size = config.db_pool_size;
  options.application_name = "identity-service";
  return common::PgPool(options);
}

void migrate(common::PgPool& pool) {
  common::run_migrations(pool, sg::identity::infra::embedded_migrations(), sg::identity::infra::kMigrationLockKey);
}

/// Seeds the demo account when the accounts table is empty. Without SEED_DEMO_PASSWORD an empty
/// database is a configuration error (the demo account is part of the contract); a non-empty one
/// needs nothing.
void seed(common::PgPool& pool, const Config& config) {
  if (!config.seed_demo_password) {
    const bool empty = pool.with_transaction(
        [](pqxx::work& tx) { return !tx.query_value<bool>("SELECT EXISTS (SELECT 1 FROM accounts)"); });
    if (empty) {
      throw std::runtime_error("environment variable SEED_DEMO_PASSWORD is required to seed the demo account "
                               "(or set SEED_ON_EMPTY=false)");
    }
    return;
  }
  if (!sg::identity::infra::seed_demo_account_if_empty(pool, *config.seed_demo_password, config.password_hash)) {
    common::log_info("demo seed skipped: accounts already exist");
  }
}

int serve(const Config& config) {
  const common::TokenIssuer issuer(config.require_signing_seed(), config.token_ttl);
  const common::TokenVerifier verifier(issuer.public_key_b64());
  if (config.password_hash_profile != "owasp") {
    common::log_warn("PASSWORD_HASH_PROFILE is not 'owasp': passwords are hashed with test-only parameters");
  }

  auto pool = make_pool(config);
  migrate(pool);
  if (config.seed_on_empty) {
    seed(pool, config);
  }

  sg::identity::infra::GraphTaxonomyClient::Options taxonomy_options;
  taxonomy_options.base_url = config.graph_service_url;
  taxonomy_options.refresh_interval = config.taxonomy_refresh;
  sg::identity::infra::GraphTaxonomyClient taxonomy(taxonomy_options);
  taxonomy.start();

  common::OutboxRelay relay(pool, config.redis, "identity.events");
  relay.start();

  sg::identity::infra::PostgresAccountRepository repository(pool);
  sg::identity::app::AccountService accounts(repository, taxonomy, issuer, config.password_hash);
  sg::identity::http::AuthRateLimiters limits(config.rate_limits);

  common::HttpServerOptions options;
  options.port = config.port;
  options.threads = config.http_threads;
  common::HttpServer server("identity-service", options);
  sg::identity::http::register_routes(server, accounts, verifier, limits);
  server.set_readiness_check([&pool] { return pool.ping(); });

  std::atomic<bool> listen_failed{false};
  std::thread http([&] {
    try {
      server.listen();
    } catch (const std::exception& e) {
      common::log_error("http server failed", {{"error", e.what()}});
      listen_failed = true;
      common::request_shutdown();
    }
  });
  if (server.wait_until_ready()) {
    common::log_info("identity-service started", {{"port", options.port}, {"graph_service_url", config.graph_service_url}});
  }
  const int signal = common::wait_for_shutdown_signal();
  common::log_info("shutting down", {{"signal", signal}});
  server.stop();
  http.join();
  relay.stop();
  taxonomy.stop();
  return listen_failed ? 1 : 0;
}

int healthcheck() {
  const int port = static_cast<int>(common::env_int("IDENTITY_PORT", 8081, 1, 65535));
  httplib::Client client("127.0.0.1", port);
  client.set_connection_timeout(2, 0);
  client.set_read_timeout(2, 0);
  const auto response = client.Get("/healthz");
  return response && response->status == 200 ? 0 : 1;
}

int keygen() {
  const auto keys = common::generate_keypair();
  std::cout << "AUTH_SIGNING_SEED=" << keys.seed_b64 << "\nAUTH_PUBLIC_KEY=" << keys.public_key_b64 << "\n";
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  common::block_shutdown_signals();
  const std::string_view command = argc > 1 ? argv[1] : "serve";
  if (argc > 2) {
    std::cerr << kUsage;
    return 2;
  }
  if (command == "--help" || command == "-h" || command == "help") {
    std::cout << kUsage;
    return 0;
  }
  try {
    if (command == "healthcheck") {
      return healthcheck();
    }
    if (command == "keygen") {
      return keygen();
    }
    common::init_logging("identity-service", common::parse_log_level(common::env_or("LOG_LEVEL", "info")));
    const Config config = Config::from_env();
    if (command == "serve") {
      return serve(config);
    }
    if (command == "migrate" || command == "seed") {
      auto pool = make_pool(config);
      migrate(pool);
      common::log_info("migrations applied");
      if (command == "seed") {
        seed(pool, config);
      }
      return 0;
    }
    std::cerr << kUsage;
    return 2;
  } catch (const std::exception& e) {
    common::log_error("fatal error", {{"error", e.what()}});
    return 1;
  }
}
