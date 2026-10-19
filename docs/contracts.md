# System contracts (source of truth for every service and the frontend)

This document fixes the boundaries between services so they can be built and deployed
independently. Any change to an endpoint, event or shared library signature must be made here first.

## 1. Services and ownership

| Service | Port | Owns (database) | Responsibility |
|---------|------|-----------------|----------------|
| `identity-service` (C++) | 8081 | `identity` DB: accounts, outbox | Registration, password verification, issuing signed access tokens |
| `graph-service` (C++) | 8082 | `graph` DB: profiles, connections, graph_meta, outbox | User profiles and the connection graph (write model, source of truth) |
| `analytics-service` (C++) | 8083 | none (derived in-memory read model) | Influencers, communities, recommendations, degrees of separation, graph export |
| `gateway` (nginx) | 8080 | — | Single public entry point, serves the SPA, routes `/api/v1/*`, rate-limits auth, hides `/internal/*` |
| `web` (React SPA) | — | — | Built static assets served by the gateway |

Infrastructure: PostgreSQL 16 (one database per service, separate roles), Redis 7 (Streams as the event bus).

Rules:
* A service never reads another service's database.
* Synchronous calls between services are limited to analytics bootstrapping from
  `GET graph-service /internal/v1/snapshot`. Everything else propagates through events.
* Every service verifies access tokens locally with the identity public key (no call to identity per request).

## 2. Request routing (gateway and Vite dev proxy)

| Path prefix | Upstream |
|-------------|----------|
| `/api/v1/auth/` | identity-service |
| `/api/v1/analytics/`, `/api/v1/paths`, `/api/v1/me/recommendations`, `/api/v1/users/{id}/metrics` | analytics-service |
| `/api/v1/users`, `/api/v1/me/profile`, `/api/v1/me/connections`, `/api/v1/meta/` | graph-service |
| `/internal/` | **never exposed** (404 at gateway) |

## 3. Conventions for every HTTP API

* JSON only (`Content-Type: application/json; charset=utf-8`), UTF-8, `snake_case` field names.
* User ids are JSON numbers (64-bit integers assigned by identity-service).
* Timestamps are RFC 3339 UTC strings, e.g. `"2026-10-19T20:00:00Z"`.
* Authentication: `Authorization: Bearer <token>`. Missing/invalid/expired → `401 unauthenticated`.
* Every response carries `X-Request-Id` (echoed from the request if present, else generated).
* Errors always use this envelope and these codes:

```json
{ "error": { "code": "validation_failed", "message": "Request validation failed",
             "details": [ { "field": "handle", "message": "must be 3-32 characters of a-z, 0-9 or _" } ] } }
```

| HTTP | `code` | When |
|------|--------|------|
| 400 | `bad_request` | Malformed JSON, wrong types, bad query parameter |
| 401 | `unauthenticated` | No/invalid token; `invalid_credentials` for a failed login |
| 404 | `not_found` | Unknown resource or route |
| 409 | `conflict` | Handle taken, already connected |
| 413 | `payload_too_large` | Body > 64 KiB |
| 422 | `validation_failed` | Well-formed but invalid values (with `details`) |
| 429 | `rate_limited` | Too many login/register attempts (`Retry-After` header) |
| 503 | `unavailable` | Dependency down / read model not ready |
| 500 | `internal` | Anything unexpected (no internals leaked) |

* Pagination: `limit` (1–100, default 20) and `offset` (≥ 0, default 0); list responses are
  `{ "items": [...], "total": N, "limit": L, "offset": O }`.
* Health: every service exposes `GET /healthz` (process alive → `{"status":"ok"}`) and
  `GET /readyz` (dependencies reachable → 200 `{"status":"ready"}` or 503).

### Taxonomy and validation (shared)

* `category` ∈ `Engineering`, `Finance`, `HR`, `Marketing`, `Sales`
* `branch` ∈ `software`, `electrical`, `accounting`, `human resources`, `marketing`, `sales`
* `handle`: `^[a-z0-9_]{3,32}$` (stored lower-case, unique)
* `name`: 1–64 characters after trimming, no control characters
* `password`: 8–128 bytes
* `influence`: number 0–100 (stored with 2 decimals)

## 4. identity-service API

`POST /api/v1/auth/register` → `201`
```json
// request
{ "handle": "ada_l", "name": "Ada Lovelace", "password": "correct horse", "category": "Engineering",
  "branch": "software", "influence": 72.5 }
// response
{ "token": "<jwt>", "expires_at": "2026-10-20T04:00:00Z",
  "user": { "id": 101, "handle": "ada_l", "name": "Ada Lovelace" } }
```
Emits `user.registered`. Errors: 409 handle taken, 422 validation, 429.

`POST /api/v1/auth/login` `{ "handle", "password" }` → `200` same body as register.
Failed login → `401 invalid_credentials` (same response and similar timing for unknown handle and wrong password).

`GET /api/v1/auth/me` (auth) → `{ "id", "handle", "name", "created_at" }`

### Tokens

JWT (RFC 7519) signed with **Ed25519** (`alg: EdDSA`), claims:
`{ "iss": "socialgraph-identity", "sub": "101", "handle": "ada_l", "name": "Ada Lovelace", "iat": 0, "exp": 0, "jti": "<random>" }`.
Identity holds the 32-byte seed (`AUTH_SIGNING_SEED`, base64); others hold the public key (`AUTH_PUBLIC_KEY`, base64).
Default TTL 8 hours (`AUTH_TOKEN_TTL_SECONDS`). Logout is client-side (stateless tokens; see ADR).

## 5. graph-service API

`UserProfile` = `{ "id", "handle", "name", "category", "branch", "influence", "created_at" }`

| Method & path | Auth | Response |
|---------------|------|----------|
| `GET /api/v1/meta/taxonomy` | no | `{ "categories": [...], "branches": [...] }` |
| `GET /api/v1/users?q=&category=&branch=&limit=&offset=` | yes | paginated `UserProfile` + `"connection_count"` per item; `q` matches handle or name (case-insensitive substring) |
| `GET /api/v1/users/{id}` | yes | `UserProfile` + `"connection_count"` |
| `GET /api/v1/me/profile` | yes | `UserProfile` + `"connection_count"`; `404 not_found` while the profile is still being provisioned after registration |
| `GET /api/v1/me/connections` | yes | `{ "items": [ { "user": UserProfile, "strength": 0.0-1.0, "connected_at" } ], "graph_version" }` sorted by strength desc |
| `POST /api/v1/me/connections` `{ "user_id": 42 }` | yes | `201 { "user": UserProfile, "strength", "connected_at", "graph_version" }`; 404 unknown user, 409 already connected, 422 self |
| `DELETE /api/v1/me/connections/{user_id}` | yes | `200 { "graph_version" }`; 404 if not connected |
| `GET /internal/v1/snapshot` | internal | `{ "graph_version", "users": [UserProfile...], "edges": [[a, b, strength], ...] }` |

`strength` = connection affinity (see §8), computed once when the connection is created.
Consumes `user.registered` (creates the profile, idempotent). Emits `user.created`,
`connection.added`, `connection.removed`.

## 6. analytics-service API

All responses include `"graph_version"` (the last graph event applied). Every endpoint accepts
`min_version=N`: the service waits up to 2 s for the read model to reach version N (read-your-writes
after a mutation) and otherwise answers with what it has plus header `X-Graph-Stale: true`.

`UserRef` = `{ "id", "handle", "name", "category", "branch" }`

| Method & path | Response |
|---------------|----------|
| `GET /api/v1/analytics/summary` | `{ graph_version, users, connections, density, average_degree, components, largest_component, communities, modularity, computed_at, compute_ms }` |
| `GET /api/v1/analytics/influencers?metric=pagerank\|betweenness\|closeness\|degree&limit=10` | `{ graph_version, metric, items: [ { rank, user: UserRef, score } ] }` |
| `GET /api/v1/analytics/communities` | `{ graph_version, modularity, items: [ { id, size, dominant_category, dominant_branch, members: [UserRef] } ] }` sorted by size desc |
| `GET /api/v1/analytics/graph` | `{ graph_version, nodes: [ { id, handle, name, category, branch, community, pagerank, degree } ], edges: [ { source, target, strength } ] }` |
| `GET /api/v1/analytics/graph.dot` | Graphviz DOT text (`text/vnd.graphviz`), nodes coloured by community |
| `GET /api/v1/me/recommendations?limit=10` | `{ graph_version, items: [ { user: UserRef, score, mutual_connections, reasons: { adamic_adar, affinity, same_category, same_branch, same_community } } ] }` |
| `GET /api/v1/paths?to=ID[&from=ID]` (`from` defaults to caller) | `{ graph_version, found, degrees, path: [UserRef] }` (`degrees` = edges on the path, `null` if not found) |
| `GET /api/v1/users/{id}/metrics` | `{ graph_version, user: UserRef, degree, pagerank, betweenness, closeness, community, ranks: { pagerank, betweenness, closeness, degree } }` |

All require auth. Unknown user ids → 404. Read model not bootstrapped yet → 503.

## 7. Events (Redis Streams)

Streams: `identity.events` (producer identity-service), `graph.events` (producer graph-service).
Each stream entry has the fields:

| field | value |
|-------|-------|
| `event_id` | producer outbox id as string (unique per stream) |
| `type` | event type below |
| `schema` | `1` |
| `occurred_at` | RFC 3339 |
| `payload` | JSON object as a string |

| type | stream | payload |
|------|--------|---------|
| `user.registered` | identity.events | `{ user_id, handle, name, category, branch, influence }` |
| `user.created` | graph.events | `{ graph_version, user: UserProfile }` |
| `connection.added` | graph.events | `{ graph_version, user_a, user_b, strength }` (`user_a < user_b`) |
| `connection.removed` | graph.events | `{ graph_version, user_a, user_b }` |

Delivery is **at-least-once** via the transactional outbox pattern: the producer writes the
event into its `outbox` table in the same transaction as the state change; a relay thread
publishes unpublished rows in id order with `XADD` and marks them published.

* graph-service consumes `identity.events` with consumer group `graph-service` (`XREADGROUP`, `XACK`
  after commit; the handler is idempotent).
* `graph_version` is gap-free and commit-ordered (`UPDATE graph_meta SET version = version + 1 RETURNING version`
  inside each write transaction). analytics-service applies events strictly in order:
  `version <= applied` → ignore (duplicate), `version == applied + 1` → apply,
  `version > applied + 1` → gap → re-bootstrap from `/internal/v1/snapshot`.
* analytics-service reads `graph.events` with plain `XREAD` (every replica sees every event; replicas
  scale horizontally).

## 8. Domain formulas (implemented in `libs/graphcore`)

* **Affinity** of two users ∈ [0,1]: `0.4·[same category] + 0.3·[same branch] + 0.3·(1 − |Δinfluence|/100)`.
* **Connection strength** = affinity at creation time, rounded to 3 decimals.
* **Louvain edge weight** = `0.5 + 0.5·strength` (structure dominates, attributes refine).
* **Recommendation score** for non-neighbour `v` of `u`:
  `0.55·AA(u,v)/max AA + 0.35·affinity(u,v) + 0.10·[same community]`, where AA is the
  Adamic–Adar index over common neighbours. Ties broken by smaller id.
* **Seed graph**: `data/seed/users.csv` (the original 100 users). Users are grouped by
  (category, branch); each user draws `avg_degree/2` edges, inside its group with probability
  `1 − μ`, else uniformly; `avg_degree = 8`, `μ = 0.2`, seed `20241109`. Deterministic across
  platforms (SplitMix64, no `std::*_distribution`).

## 9. Repository layout and build

```
CMakeLists.txt  CMakePresets.json        top-level C++ build (C++20)
libs/graphcore/                          pure algorithms, namespace sg::graph, no I/O
libs/common/                             shared service runtime, namespace sg::common
services/identity|graph|analytics/       one executable each, own migrations/ and tests/
frontend/                                React + TypeScript + Vite
gateway/                                 nginx config + Dockerfile (also builds the SPA)
deploy/                                  docker-compose.yml, .env.example
data/seed/users.csv                      seed data
tests/e2e/                               Playwright end-to-end tests
docs/                                    architecture, ADRs, API, events, performance, testing
legacy/                                  untouched original implementation
```

System packages (Ubuntu 24.04; same in Dockerfiles and CI): `g++` 13 or `clang` 18, `cmake` ≥ 3.28,
`ninja-build`, `libcpp-httplib-dev` (0.14.3), `nlohmann-json3-dev`, `libpqxx-dev` (7.8),
`libsodium-dev`, `libhiredis-dev`, `libgtest-dev`, `libbenchmark-dev`.

C++ conventions: C++20, `-Wall -Wextra -Wpedantic -Werror` in CI, no `using namespace` in headers,
`snake_case` functions/variables, `PascalCase` types, headers under `include/sg/<lib>/`,
RAII everywhere (no owning raw pointers), errors as exceptions inside services
(`sg::common::ApiError`), GoogleTest for tests (`ctest` discovers them).
