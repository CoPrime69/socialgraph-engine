# Architecture decision records

| # | Decision | Status |
|---|----------|--------|
| [0001](0001-microservices-with-database-per-service.md) | Split into identity, graph and analytics services with a database per service | Accepted |
| [0002](0002-cpp20-and-cpp-httplib.md) | C++20 services on cpp-httplib | Accepted |
| [0003](0003-postgresql-for-system-of-record.md) | PostgreSQL (libpqxx) as the system of record | Accepted |
| [0004](0004-redis-streams-and-transactional-outbox.md) | Redis Streams + transactional outbox for events | Accepted |
| [0005](0005-stateless-ed25519-tokens.md) | Stateless Ed25519-signed JWTs verified in every service | Accepted |
| [0006](0006-analytics-as-cqrs-read-model.md) | Analytics as an in-memory, versioned CQRS read model | Accepted |
| [0007](0007-louvain-community-detection.md) | Louvain modularity optimisation for communities | Accepted |
| [0008](0008-nginx-gateway.md) | nginx as API gateway and static host | Accepted |
| [0009](0009-frontend-stack.md) | React + TypeScript + Vite + TanStack Query, canvas graph | Accepted |
