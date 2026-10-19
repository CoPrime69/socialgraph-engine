# identity-service

Registration, password verification and stateless Ed25519 access tokens (contracts.md §4, §10.2,
§10.4). Owns the `identity` database (`accounts`, `outbox`); every other service verifies tokens
locally with the public key.

| Route | Auth | Response |
|-------|------|----------|
| `POST /api/v1/auth/register` | – | `201 { token, expires_at, user: { id, handle, name } }` |
| `POST /api/v1/auth/login` | – | `200` same body; `401 invalid_credentials` on failure |
| `GET /api/v1/auth/me` | Bearer | `{ id, handle, name, created_at }` |
| `GET /healthz`, `GET /readyz` | – | liveness; readiness = PostgreSQL reachable |

Register body: `{ handle, name, password, category, branch }` plus optional `location` (≤ 100
characters) and `about` (≤ 500 characters, line breaks allowed); `influence` is ignored. All
failing fields are reported at once (`422 validation_failed` with `details`); a field of the wrong
JSON type is `400`, a taken handle `409`. `category` and `branch` must be names from graph-service's
`GET /api/v1/meta/taxonomy`: a background thread fetches it from `GRAPH_SERVICE_URL` at startup,
retries every 2 s until graph-service answers with a non-empty taxonomy, then refreshes it every
`TAXONOMY_REFRESH_SECONDS`. While no taxonomy has been loaded, register answers `503 unavailable`
(login keeps working).

## Security

* Passwords: Argon2id (libsodium), OWASP parameters m = 19 MiB, t = 2, p = 1
  (`PASSWORD_HASH_PROFILE=owasp`, the default; `fast` exists for tests only and logs a warning).
  Hashes with outdated parameters are transparently upgraded at the next successful login.
* Unknown handle and wrong password produce byte-identical `401 invalid_credentials` responses and
  cost one Argon2id verification each (a dummy hash is verified for unknown handles). Malformed
  handles and passwords over 128 bytes are never looked up.
* Tokens: JWT `alg: EdDSA` with `iss`, `sub`, `handle`, `name`, `iat`, `exp` (default 8 h) and a
  random `jti`; verification accepts only EdDSA, the expected issuer and ±30 s clock skew.
* Rate limits on top of the gateway's, answered with `429 rate_limited` and `Retry-After`: login and
  register attempts per client address (first `X-Forwarded-For` hop, which the gateway overwrites)
  and failed logins per handle (successful logins are not counted, so the shared demo account is
  only throttled while someone is guessing its password).
* No secrets in logs: the access log has method, path, status, duration and request id only.

## Events

`user.registered` `{ user_id, handle, name, category, branch, location, about }` is written to
`outbox` in the same transaction as the account and published to the Redis stream
`identity.events` by the outbox relay (leader-elected with a PostgreSQL advisory lock;
at-least-once). graph-service consumes it with the consumer group `graph-service`.

## Ids and seeding

`accounts.id` is an identity column starting at 100,000,000 (imported Stack Overflow users, which
are graph profiles and cannot sign in, keep their ids below that). The migration reserves
100,000,000 for the demo account, so self-registered members get 100,000,001 onwards whether or not
the demo account is seeded. With `SEED_ON_EMPTY=true` (default), `serve` creates the demo account
(`demo` / "Demo Member" / `SEED_DEMO_PASSWORD`) when the table is empty; no event is emitted because
graph-service seeds the matching profile itself. An empty database with seeding enabled and no
`SEED_DEMO_PASSWORD` is a startup error.

## Configuration

| Variable | Default | |
|----------|---------|---|
| `IDENTITY_PORT` | `8081` | |
| `IDENTITY_DATABASE_URL` | – | required for `serve`, `migrate`, `seed` |
| `AUTH_SIGNING_SEED` | – | base64 32-byte Ed25519 seed (secret), required for `serve` |
| `AUTH_TOKEN_TTL_SECONDS` | `28800` | 60 s – 30 days |
| `PASSWORD_HASH_PROFILE` | `owasp` | `owasp` or `fast` (tests) |
| `REDIS_HOST` / `REDIS_PORT` | `127.0.0.1` / `6379` | |
| `GRAPH_SERVICE_URL` | `http://localhost:8082` | taxonomy source |
| `TAXONOMY_REFRESH_SECONDS` | `300` | |
| `SEED_ON_EMPTY` | `true` | seed the demo account into an empty database |
| `SEED_DEMO_PASSWORD` | – | 8–128 bytes (secret) |
| `HTTP_THREADS` / `DB_POOL_SIZE` | `8` / `8` | |
| `RATE_LIMIT_WINDOW_SECONDS` | `60` | |
| `LOGIN_RATE_LIMIT_PER_IP` | `30` | login attempts per client and window; `0` disables |
| `LOGIN_RATE_LIMIT_PER_HANDLE` | `10` | failed logins per handle and window; `0` disables |
| `REGISTER_RATE_LIMIT_PER_IP` | `20` | `0` disables |
| `LOG_LEVEL` | `info` | |

Subcommands: `serve` (default), `migrate`, `seed`, `keygen` (prints a fresh
`AUTH_SIGNING_SEED`/`AUTH_PUBLIC_KEY` pair), `healthcheck` (exit 0 when `/healthz` on
`127.0.0.1:$IDENTITY_PORT` answers 200), `--help`.

```sh
set -a; eval "$(identity-service keygen)"; set +a   # dev only; deployments use scripts/generate-env.sh
IDENTITY_DATABASE_URL=postgresql://socialgraph:devpassword@localhost:5432/identity \
SEED_DEMO_PASSWORD='demo-password-123' identity-service serve
```

## Code layout

`src/sg/identity/domain` (account, registration rules, event payload) → `app` (use cases,
`AccountRepository` and `TaxonomyProvider` ports) → `infra` (PostgreSQL repository, demo seed,
graph taxonomy client, embedded migrations from `migrations/`) → `http` (routes only).
`src/main.cpp` wires them together.

## Tests

* `identity_unit_tests` (label `unit`): registration rules, `AccountService` with an in-memory
  repository, the HTTP API on an ephemeral port (status codes, envelopes, 413, 429 + Retry-After,
  token checks) and the taxonomy client against a fake graph-service.
* `identity_integration_tests` (label `integration`, needs `SG_TEST_DATABASE_URL`; the Redis test
  also `SG_TEST_REDIS_HOST`): a throw-away database per test; account + outbox atomicity, rollback,
  concurrent duplicate registrations, schema constraints, demo seeding (concurrent, id sequence)
  and relay publication to Redis.
