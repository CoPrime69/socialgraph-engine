# Phase 2 — Plan and implementation roadmap

## Target architecture (microservices, revised)

The first draft proposed a modular monolith. Following the project owner's direction, the system is
split into independently deployable services with their own data, communicating through versioned
HTTP contracts and events. The authoritative contract is [`contracts.md`](contracts.md).

```
                      ┌──────────────────────────── gateway (nginx :8080) ───────────────────────────┐
 Browser (React SPA) ─┤  /            → static SPA                                                    │
                      │  /api/v1/auth → identity   /api/v1/{users,me/profile,me/connections,meta} → graph │
                      │  /api/v1/{analytics,paths,me/recommendations,users/:id/metrics} → analytics   │
                      └───────────────────────────────────────────────────────────────────────────────┘
   identity-service (C++)        graph-service (C++)                 analytics-service (C++)
   accounts, Argon2id,           profiles + connections,             in-memory read model,
   Ed25519 JWT issuing           write model, graph_version          PageRank/Brandes/Louvain/recs/paths
        │ PostgreSQL "identity"       │ PostgreSQL "graph"                 │ (no database)
        └─ outbox ─▶ Redis Stream identity.events ─▶ graph-service        │
                      graph-service outbox ─▶ Redis Stream graph.events ─▶┘ (+ HTTP snapshot bootstrap)
```

* Each service owns its database; none reads another's tables.
* Tokens are verified locally in every service with the identity public key (Ed25519), so identity is
  not on the hot path of other requests.
* Analytics is a CQRS read model: rebuilt from a snapshot, kept current by ordered events, scaled by
  adding replicas.
* Shared C++ code lives in two libraries: `libs/graphcore` (pure algorithms) and `libs/common`
  (HTTP bootstrap, logging, config, errors, tokens, PostgreSQL pool, Redis Streams, outbox relay).

## Algorithms

| Feature | Algorithm | Complexity |
|---------|-----------|------------|
| Graph storage | Compressed sparse row (CSR) snapshot, dense `uint32` ids | O(V+E) memory |
| PageRank | Power iteration, uniform dangling redistribution, L1 tolerance | O(k(V+E)) |
| Betweenness | Brandes, parallel over sources; optional pivot sampling for large graphs | O(VE) exact |
| Closeness | BFS per node, Wasserman–Faust normalisation for disconnected graphs, parallel | O(V(V+E)) |
| Communities | Louvain (weighted modularity), deterministic | ~O(E log V) in practice |
| Recommendations | Friends-of-friends Adamic–Adar + attribute affinity, top-k selection | O(Σ deg² of neighbourhood + k log k) |
| Degrees of separation | Bidirectional BFS | O(b^(d/2)) |

## API (v1, JSON)

`POST /auth/register`, `POST /auth/login`, `POST /auth/logout`, `GET /me`, `GET /users`,
`GET /users/{handle}`, `GET|POST /me/connections`, `DELETE /me/connections/{handle}`,
`GET /me/recommendations`, `GET /graph`, `GET /graph.dot`, `GET /analytics/summary`,
`GET /analytics/influencers`, `GET /analytics/communities`, `GET /paths`, `GET /meta`,
`GET /health`, `GET /ready`. Errors: `{"error": {"code", "message", "details"}}`.

## Database

`users`, `connections (user_a < user_b, FK cascade, unique pair)`, `sessions (sha256(token), expiry)`,
`schema_migrations`. Versioned SQL migrations applied at start-up under an advisory lock.

## Testing strategy

Unit (core algorithms vs brute-force reference implementations), application services with
in-memory repositories, HTTP API tests against an in-process server, PostgreSQL integration tests,
concurrency test under ThreadSanitizer, frontend component tests, Playwright end-to-end journeys.

## Milestones (revised for microservices)

1. Contracts (`contracts.md`), build skeleton, `libs/graphcore` with tests and micro-benchmarks.
2. `libs/common`: HTTP bootstrap, JSON logging, config, errors, Ed25519 tokens, PostgreSQL pool and migrations, Redis Streams, outbox relay.
3. identity-service and graph-service with migrations, seed and tests.
4. analytics-service read model with event consumption and snapshot bootstrap.
5. React frontend wired to the real APIs through the gateway.
6. Docker images, Compose, GitHub Actions, end-to-end tests.
7. Benchmarks and load tests, documentation, final review.
