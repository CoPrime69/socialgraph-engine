# Architecture decision records

| # | Decision | Status |
|---|----------|--------|
| [0001](0001-microservices-with-database-per-service.md) | Split into identity, graph, analytics and content services with a database per service | Accepted |
| [0002](0002-cpp20-and-drogon.md) | C++20 services on Drogon (replaced cpp-httplib) | Accepted |
| [0003](0003-postgresql-for-system-of-record.md) | PostgreSQL (libpqxx) as the system of record | Accepted |
| [0004](0004-redis-streams-and-transactional-outbox.md) | Redis Streams + transactional outbox for events | Accepted |
| [0005](0005-stateless-ed25519-tokens.md) | Stateless Ed25519-signed JWTs verified in every service | Accepted |
| [0006](0006-analytics-as-cqrs-read-model.md) | Analytics as an in-memory, versioned CQRS read model | Accepted |
| [0007](0007-louvain-community-detection.md) | Louvain modularity optimisation for communities | Accepted |
| [0008](0008-nginx-gateway.md) | nginx as API gateway and static host | Accepted |
| [0009](0009-frontend-stack.md) | React + TypeScript + Vite + TanStack Query, canvas graph | Accepted |
| [0010](0010-read-caches-and-invalidation.md) | In-process read caches, invalidated by events; searches as id lists | Accepted |
| [0011](0011-group-commit-of-connection-writes.md) | Group commit of connection writes through one writer thread | Accepted |
| [0012](0012-two-tier-analytics-refresh.md) | Two-tier analytics refresh: structure often, global scores at most every 30 s | Accepted |
| [0013](0013-mimalloc.md) | mimalloc as the services' allocator | Accepted |
