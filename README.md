# SocialGraph Engine

A social network analyzer rebuilt as a production-style system: four C++20 microservices behind an
nginx gateway, PostgreSQL per service, Redis Streams for events, and a React + TypeScript front
end. It runs on real data, the first months of Stack Overflow (August-December 2008: about 18,600
members, 205,000 interaction edges, 57,000 questions, 208,000 answers), and answers the original
project's questions: who the influencers are, which communities exist, whom to connect with next,
and how far apart two people are.

It started as a single-file console program (kept under [`legacy/`](legacy/)); the audit of that
program and the plan for this one are in [`docs/audit.md`](docs/audit.md) and
[`docs/roadmap.md`](docs/roadmap.md).

## Architecture

```
 Browser (React SPA)
        │
        ▼
 nginx gateway :8080 ── static SPA, routing, rate limits, security headers
        │
        ├── /api/v1/auth/*                         → identity-service  :8081  (PostgreSQL "identity")
        ├── /api/v1/users, /me/profile, /me/connections → graph-service :8082  (PostgreSQL "graph")
        ├── /api/v1/analytics/*, /paths, /me/recommendations, /users/:id/metrics
        │                                           → analytics-service :8083 (in-memory read model)
        └── /api/v1/users/:id/activity, /tags, /questions → content-service :8084 (PostgreSQL "content")

 identity ──outbox──▶ Redis Stream identity.events ──▶ graph (creates the member's profile)
 graph    ──outbox──▶ Redis Stream graph.events    ──▶ analytics (read model), graph replicas (cache invalidation)
```

* **Database per service**; no service reads another's tables ([ADR-0001](docs/adr/0001-microservices-with-database-per-service.md)).
* **Transactional outbox + Redis Streams**: a write and its event commit together, and consumers
  see gap-free, versioned events ([ADR-0004](docs/adr/0004-redis-streams-and-transactional-outbox.md)).
* **Stateless Ed25519 tokens**, verified locally by every service, so identity is not on the path
  of other requests ([ADR-0005](docs/adr/0005-stateless-ed25519-tokens.md)).
* **Analytics as a CQRS read model**: an immutable, versioned in-memory graph snapshot, bootstrapped
  over HTTP and kept current from events; no database ([ADR-0006](docs/adr/0006-analytics-as-cqrs-read-model.md)).
* **Read caches invalidated by events**, group commit of writes, and a two-tier analytics refresh
  ([ADR-0010](docs/adr/0010-read-caches-and-invalidation.md),
  [0011](docs/adr/0011-group-commit-of-connection-writes.md),
  [0012](docs/adr/0012-two-tier-analytics-refresh.md)).

All service contracts (routes, payloads, events, error envelope) are in
[`docs/contracts.md`](docs/contracts.md); every decision has an ADR in [`docs/adr/`](docs/adr/).

## Features

| Feature | How |
|---|---|
| Accounts | Registration and login, Argon2id password hashes, per-IP and per-handle rate limits |
| Profiles and search | Search by name or handle (trigram indexes), filters, sorting, pagination |
| Connections | Connect and disconnect, with strength from shared expertise; every change is a versioned event |
| Influencers | PageRank, betweenness (Brandes, sampled above 5,000 nodes), closeness, degree |
| Communities | Louvain modularity optimisation, deterministic |
| Recommendations | Friends-of-friends (Adamic-Adar) blended with expertise affinity, with reasons |
| Degrees of separation | Bidirectional BFS between any two members |
| Activity | Each member's Stack Overflow questions, answers, tags and badges; questions by tag and full-text search |
| Visualisation | Interactive network and ego-network graphs in the browser |

The graph algorithms live in [`libs/graphcore`](libs/graphcore), a dependency-free C++ library
with its own tests and micro-benchmarks ([`docs/performance-graphcore.md`](docs/performance-graphcore.md)).

## Performance

On a mixed, randomised workload that uses every feature (6 % writes), run through the gateway with
autocannon on a 4-vCPU VM that also hosts PostgreSQL, Redis, nginx and the load generator:

| | Throughput | p99 latency | Errors |
|---|---:|---:|---:|
| First complete build | 868 req/s | 1,390.6 ms | 0 |
| Current | **6,554 req/s** | **64.6 ms** | **0** |

The steps in between (event-loop HTTP server, group commit, prepared statements, caches answered on
the event loop, mimalloc, search id lists), each measured, are in
[`docs/performance.md`](docs/performance.md), with the raw results in
[`tools/loadtest/results/`](tools/loadtest/results/).

## Run it

Requirements: Docker with Compose v2.

```bash
./scripts/generate-env.sh          # writes deploy/.env with random secrets and prints the demo password
docker compose -f deploy/docker-compose.yml --env-file deploy/.env up --build -d
# open http://localhost:8080 and sign in as "demo", or register
```

The first start imports the dataset into empty databases.

## Develop

Backend (Ubuntu 24.04 packages; the CI workflow installs the same list):

```bash
sudo apt-get install g++ cmake ninja-build pkg-config libdrogon-dev libjsoncpp-dev libcpp-httplib-dev \
  nlohmann-json3-dev libpqxx-dev libpq-dev libsodium-dev libhiredis-dev libgtest-dev libbenchmark-dev libmimalloc-dev
cmake --preset debug && cmake --build --preset debug
# Integration tests need PostgreSQL and Redis; without these variables they are skipped.
SG_TEST_DATABASE_URL=postgresql://user:pass@localhost:5432/postgres SG_TEST_REDIS_HOST=127.0.0.1 \
  ctest --preset debug
```

Presets: `debug`, `release`, `ci` (warnings as errors), `asan` (AddressSanitizer + UBSan), `tsan`
(ThreadSanitizer). Front end: see [`frontend/README.md`](frontend/README.md).

## Repository layout

```
libs/graphcore     graph algorithms (CSR graph, PageRank, Brandes, closeness, Louvain, recommendations, BFS)
libs/common        shared service code: HTTP server (Drogon), tokens, PostgreSQL pool and migrations,
                   outbox, Redis Streams, LRU cache, logging, config
services/          identity, graph, analytics, content (each: src, migrations, tests, README)
frontend/          React + TypeScript + Vite single-page app
gateway/           nginx configuration and image
deploy/            Dockerfile for the services, docker-compose.yml, env template
data/stackoverflow the dataset (CC BY-SA 4.0) and its build script; see docs/dataset.md
tools/loadtest     mixed-workload load test and the local stack it runs against
docs/              contracts, ADRs, audit, dataset, performance
legacy/            the original console program
```

## Testing and CI

GitHub Actions builds and tests every push with GCC and Clang (optimised, warnings as errors), under
AddressSanitizer + UndefinedBehaviorSanitizer and ThreadSanitizer, against real PostgreSQL 16 and
Redis 7 service containers; it also runs a benchmark smoke pass and the front end's lint, type
check, unit tests with coverage and production build.

## Data

Stack Overflow data dump, August-December 2008, licensed CC BY-SA 4.0 by Stack Exchange Inc. and
its contributors. Details and the mapping to the social-network model are in
[`docs/dataset.md`](docs/dataset.md).
