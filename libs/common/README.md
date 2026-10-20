# sg::common: shared service runtime

Static library `sg_common` (alias `sg::common`, namespace `sg::common`, headers
`include/sg/common/*.hpp`) used by every C++ service. It holds the cross-cutting plumbing so that
services contain only their domain: configuration, logging, errors, HTTP bootstrap, tokens,
passwords, PostgreSQL, Redis Streams, events, the transactional outbox and CSV import.

| Header | Provides |
|--------|----------|
| `config.hpp` | `env_get`, `env_or`, `env_required`, `env_int` (range-checked), `env_bool`; an empty variable counts as unset |
| `log.hpp` | one JSON object per line on stdout (`ts`, `level`, `service`, `msg`, fields); levels; `set_log_sink` for tests |
| `errors.hpp` | `ApiError` (status, stable code, message, field details, `Retry-After`), factories for every code of contracts §3, `Validator` |
| `http.hpp` | `HttpServer` on Drogon's event loops (ADR-0002): own `Request`/`Response` types with cpp-httplib member names, `:param` routing, handlers on a worker pool or inline (`blocking_handlers`, `offload`), request ids, access log, JSON 404/413, error mapping, `/healthz` + `/readyz`, graceful `stop()`, `bind_to_any_port()` for tests; `parse_json_body`, `write_json`, `query_int`, `path_id`, `client_ip` |
| `auth.hpp` | Ed25519 JWT (`alg: EdDSA`): `TokenIssuer`, `TokenVerifier` (EdDSA only, issuer, ±30 s skew), `require_auth`, `generate_keypair` |
| `crypto.hpp` | Argon2id `hash_password` / `verify_password` / `password_needs_rehash` (OWASP parameters), random tokens, base64/base64url |
| `pg.hpp` | `PgPool` (bounded, lazy, health-checked; 503 when exhausted or unreachable), `with_transaction`, `run_migrations` (advisory-locked, idempotent, rejects rewritten history) |
| `pqxx.hpp` | the only way to include libpqxx (see below) |
| `redis.hpp` | `RedisClient` for Streams: `XADD` (+MAXLEN), `XREAD`, `XGROUP CREATE`, `XREADGROUP`, `XACK`, `XAUTOCLAIM`, `XRANGE`, `XLEN`, `DEL`, `PING`; reconnects lazily |
| `events.hpp` | the stream envelope of contracts §7 (`event_id`, `type`, `schema`, `occurred_at`, `payload`) |
| `outbox.hpp` | `enqueue_event(tx, type, payload)` and `OutboxRelay` (leader-elected via `pg_try_advisory_lock`, in-order XADD, backoff, retention) |
| `rate_limit.hpp` | bounded fixed-window `RateLimiter`; `retry_after()` checks a key without counting an attempt |
| `lru_cache.hpp` | header-only `LruCache<Key, Value>`: thread-safe, size-bounded, sharded by key hash (`get`, `put`, `get_or_load`, `stats`); content-service caches rendered responses with it |
| `validation.hpp` | handle/name/password rules and messages shared by identity and graph, UTF-8 checks, rounding |
| `csv.hpp` | RFC 4180 `parse_csv` and streaming `CsvReader` (transparent gzip) with file:line errors |
| `time.hpp` | RFC 3339 formatting, Unix time |
| `lifecycle.hpp` | `block_shutdown_signals`, `wait_for_shutdown_signal`, `request_shutdown` |
| `migration.hpp` | `struct Migration { version, name, sql }` |

## Build integration

* `cmake/EmbedMigrations.cmake` defines
  `sg_embed_migrations(<target> <dir> <namespace>)`: compiles `<dir>/NNNN_name.sql` into
  `std::span<const sg::common::Migration> <namespace>::embedded_migrations()` (versions strictly
  increasing, re-run on change), so binaries carry their schema.
* Dependencies: nlohmann_json, libpqxx, cpp-httplib (PUBLIC); Drogon, libsodium, hiredis, zlib
  (PRIVATE).
* Drogon (`libdrogon-dev` 1.8.7, with `libtrantor-dev` and `libjsoncpp-dev`) is only included by
  `src/http.cpp`; services never see its types. Ubuntu's `DrogonConfig.cmake` requires the
  development packages of every database backend Drogon was built with (MySQL, SQLite, yaml-cpp, ...),
  so `CMakeLists.txt` finds `libdrogon`, `Trantor` and jsoncpp (headers under
  `/usr/include/jsoncpp`) directly instead. Runtime images need `libdrogon1t64`, which pulls in
  `libtrantor1` and `libjsoncpp25`.
* `HttpServer` drives the process-wide `drogon::app()`, which cannot be restarted: the first
  `listen()` starts it with that server's `threads`, `idle_timeout` and body limit, and its
  listeners are fixed from then on. Requests are routed to servers by listener port. A process with
  one server (production) quits the engine when it stops; when ephemeral ports were used
  (`bind_to_any_port()`, tests), the engine also opens 8 spare ephemeral listeners on that host
  for later servers and runs until the process exits. Drogon's own SIGINT/SIGTERM handling is disabled; the
  services keep `lifecycle.hpp`.
* ThreadSanitizer: the distro Drogon/trantor libraries are not instrumented, so TSan cannot see
  hand-offs through trantor's lock-free task queue. `http.cpp` queues work on an event loop only
  through `queue_in_loop()`, which under TSan publishes the hand-off with
  `__tsan_release`/`__tsan_acquire`; the queue's internal nodes are suppressed by
  `cmake/tsan.supp` (`TSAN_OPTIONS=suppressions=...`, set in CI). Run TSan tests with it.
* cpp-httplib serves as HTTP client only (service-to-service calls, `healthcheck`).
  Ubuntu's `libcpp-httplib-dev` 0.14.3 ships a split header (declarations only) plus
  `libcpp-httplib.so` built with OpenSSL, zlib and brotli support. `sg::common` therefore links
  `PkgConfig::CPPHTTPLIB`, which also carries the matching `CPPHTTPLIB_*` definitions: every target
  that includes `<httplib.h>` must get them through `sg::common` (or the same pkg-config module), or
  the class layouts differ from the library's. Runtime images need `libcpp-httplib0.14t64`.
* Ubuntu's libpqxx 7.8 is compiled as C++17. Included from C++20 its headers switch exception
  constructors to `std::source_location` overloads that the library does not contain (link errors
  and ABI mismatch). `sg/common/pqxx.hpp` hides `__cpp_lib_source_location` while including
  `<pqxx/pqxx>`; always include that header instead of `<pqxx/...>`.
  The wrapper also ignores `-Wmaybe-uninitialized` for the lines of the pqxx headers: GCC 13 at
  `-O2`/`-O3` reports a false positive inside `pqxx::params::append<T>()` (the move of its
  `std::variant` entry) wherever `exec_params` gets a non-string argument, which broke
  `-Werror` Release builds. The warning stays enabled for the including code.

## Testing helpers

INTERFACE target `sg::common_testing` (`testing/include/sg/common/testing/test_env.hpp`):
`test_database_url()` (`SG_TEST_DATABASE_URL`), `test_redis_options()` (`SG_TEST_REDIS_HOST`,
`SG_TEST_REDIS_PORT`), `TempDatabase` (a database per test, dropped `WITH (FORCE)`; the role needs
`CREATEDB`) and `unique_name()` for stream names. Integration tests call `GTEST_SKIP()` when the
variables are unset.

## Tests

* `sg_common_tests` (label `unit`): config, logging, errors, HTTP server behaviour (ids, 404/413,
  error mapping, no secrets in logs, graceful stop, busy port, routing precedence, inline vs worker
  dispatch, servers created and destroyed in one process), tokens (tampering, `alg: none`,
  HS256 confusion, expiry, skew), Argon2id, base64, rate limiter, validation, CSV and gzip, events.
* `sg_common_integration_tests` (label `integration`): pool exhaustion, statement timeout, broken
  connection replacement, unreachable database without leaked credentials, migrations (idempotent,
  concurrent, rollback on failure, rewritten history), Redis Streams commands (consumer groups,
  redelivery, `XAUTOCLAIM`, reconnect) and the outbox relay (rollback leaves no event, in-order
  publication, batching, single leader with follower take-over, prompt stop, Redis outage retried).
