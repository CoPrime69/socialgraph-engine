# graph-service

The write model of the social graph: user profiles, the category/branch taxonomy and connections
(contracts.md §5, §7, §10.3, §10.5). Owns the `graph` database; publishes `graph.events` and
consumes `identity.events`.

| Route | Auth | Response |
|-------|------|----------|
| `GET /api/v1/meta/taxonomy` | – | `{ categories: [names], branches: [names], branch_categories: { branch: category } }`, display order |
| `GET /api/v1/users?q=&category=&branch=&sort=&min_reputation=&limit=&offset=` | Bearer | `{ items: [UserProfile + connection_count], total, limit, offset }` |
| `GET /api/v1/users/{id}` | Bearer | `UserProfile + connection_count`; 404 |
| `GET /api/v1/me/profile` | Bearer | the caller's profile; 404 while it is being provisioned after registration |
| `GET /api/v1/me/connections` | Bearer | `{ items: [{ user, strength, interactions, origin, connected_at, first_interaction_at, last_interaction_at }], graph_version }`, strength desc, ties by id |
| `POST /api/v1/me/connections` `{ "user_id": N }` | Bearer | `201` connection item + `graph_version`; 404 unknown user, 409 already connected, 422 self |
| `DELETE /api/v1/me/connections/{user_id}` | Bearer | `{ graph_version }`; 404 when not connected |
| `GET /internal/v1/snapshot` | internal | `{ graph_version, users: [UserProfile], edges: [[a, b, strength]] }` (one REPEATABLE READ snapshot) |
| `GET /healthz`, `GET /readyz` | – | liveness; readiness = PostgreSQL reachable |

`UserProfile` = `{ id, handle, name, category, branch, influence, reputation, location, website_url,
about, views, up_votes, down_votes, source, external_url, created_at, last_access_at }`.
For the full dataset the snapshot is about 12.7 MB of JSON and is served in 0.6–0.9 s.
`/users` parameters: `q` is a case-insensitive substring of handle or name (≤ 100 characters; `%`
and `_` match literally), `category`/`branch` must exist in the taxonomy (400 otherwise),
`sort=reputation` (default, desc) | `name` | `id`, ties by id; `min_reputation` ≥ 0; `limit` 1–100
(default 20), `offset` ≥ 0. Every other malformed parameter is `400 bad_request`.

## Data model

`migrations/`: `categories` and `branches` (ids = display order = file order), `profiles` (FKs to
the taxonomy names; imported ids < 100,000,000, member ids ≥ 100,000,000 enforced by a CHECK;
handles unique per source, because identity cannot see imported handles), `connections` (stored once
with `user_a < user_b`, `ON DELETE CASCADE`, `strength numeric(4,3)` in [0, 1], `origin` imported |
member), `graph_meta` (one row, `version`), `outbox`, `processed_events` and `dead_letter_events`.
Indexes serve the queries: `(reputation DESC, user_id)` and its `category`/`branch`-prefixed
variants for the default listing and filters, `connections (user_b)` for the reverse direction, and
`pg_trgm` GIN indexes on `lower(handle)` / `lower(name)` for `q` (measured on the full dataset: a
`count(*)` for `q=jon` drops from ~24 ms with a sequential scan to ~3 ms; `q=skeet` takes 0.4 ms).

## graph_version and events

Every write transaction first locks the `graph_meta` row (`SELECT … FOR UPDATE`), so writers are
serialised; it bumps `version` exactly once if (and only if) it changes the graph and writes the
event carrying that version to `outbox` in the same transaction. Versions are therefore gap-free,
and outbox id order = version order = commit order; failed or no-op writes never consume a version.
The outbox relay publishes to `graph.events`: `user.created { graph_version, user }`,
`connection.added { graph_version, user_a, user_b, strength }`,
`connection.removed { graph_version, user_a, user_b }`.

Member-created connections get strength = affinity (contracts §8) computed by
`sg::graph::connection_strength` with category/branch codes = 0-based display-order index; imported
connections keep the precomputed strength from `connections.csv`.

## Consuming identity.events

Consumer group `graph-service` (created from the start of the stream, so registrations published
before graph-service first ran are applied), consumer name `CONSUMER_NAME` (default: host name).
`user.registered` creates the member profile (reputation 1, influence 0) and emits `user.created`.
The handler is idempotent: `(stream, event_id)` is recorded in `processed_events` in the same
transaction, and an existing profile is never recreated. An entry is acknowledged only after commit.
On start the consumer re-reads its own pending entries; every 30 s it claims entries other consumers
left pending for over 60 s (`XAUTOCLAIM`). Malformed events and events the schema rejects (unknown
branch, handle clash) are stored in `dead_letter_events` and acknowledged; database/Redis outages
are retried with exponential backoff (≤ 5 s) and never dead-lettered.

## Seeding

With `SEED_ON_EMPTY=true` (default) `serve` imports `SEED_DIR` (default `data/stackoverflow`; files
`categories`, `branches`, `users`, `connections` as `.csv.gz` or `.csv`) into an empty database: one
transaction, `COPY` through `pqxx::stream_to`, every row validated first (errors name file and line;
nothing is committed), advisory lock against concurrent replicas. It then adds the demo member
(id 100000000, `demo`, "Demo Member", Web Development / `javascript`) connected (origin `member`,
strength = affinity) to the 20 highest-reputation imported `javascript` users, and sets
`graph_version` to 1 without emitting events (consumers bootstrap from the snapshot). The full
dataset (18,648 users, 204,650 connections) imports in about 5 s (5.1 s RelWithDebInfo,
5.7 s Debug; most of it is PostgreSQL building indexes and `ANALYZE`).
`graph-service seed` does the same on demand.

## Configuration

| Variable | Default | |
|----------|---------|---|
| `GRAPH_PORT` | `8082` | |
| `GRAPH_DATABASE_URL` | – | required for `serve`, `migrate`, `seed` |
| `AUTH_PUBLIC_KEY` | – | identity's base64 Ed25519 public key, required for `serve` |
| `REDIS_HOST` / `REDIS_PORT` | `127.0.0.1` / `6379` | |
| `SEED_ON_EMPTY` | `true` | |
| `SEED_DIR` | `data/stackoverflow` | images mount the dataset at `/data/stackoverflow` |
| `CONSUMER_NAME` | host name | unique per replica |
| `HTTP_THREADS` / `DB_POOL_SIZE` | `8` / `8` | |
| `LOG_LEVEL` | `info` | |

Subcommands: `serve` (default), `migrate`, `seed`, `healthcheck`, `--help`. The migrations create
the `pg_trgm` extension, which PostgreSQL ≥ 13 lets the database owner do.

## Code layout

`src/sg/graph_service/domain` (profiles, taxonomy, affinity, query rules, event payloads) → `app`
(`GraphService` use cases and the `GraphRepository` port) → `infra` (PostgreSQL repository, dataset
import, identity.events consumer, embedded migrations) → `http` (routes only). `src/main.cpp` wires
them together.

## Tests

* `graph_unit_tests` (label `unit`): domain rules and JSON shapes, `GraphService` with an in-memory
  repository, and the HTTP API on an ephemeral port.
* `graph_integration_tests` (label `integration`; `SG_TEST_DATABASE_URL`, plus
  `SG_TEST_REDIS_HOST` for the consumer and event-flow tests): fixture import (`tests/fixtures`),
  import of the real dataset, rejection of broken files, schema constraints, version bump + outbox
  atomicity, rollback leaving no outbox row, 8 concurrent writers producing gap-free versions in
  outbox order, idempotent and poison-event handling, consumer acknowledgement, retry and claiming of
  stale entries, and the cross-service flow identity register → relay → graph consumer →
  `graph.events` `user.created`.
