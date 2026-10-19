# ADR-0001: Microservices with a database per service

## Context
The original program was a single console executable mixing I/O, persistence and algorithms. The
first plan (see `docs/roadmap.md`) proposed a modular monolith. The project owner asked for the
system to be split into highly decoupled microservices.

The domain has three responsibilities with different change rates and resource profiles:
* **Identity** – credentials and token issuing; security-sensitive, tiny data, CPU spikes from Argon2id.
* **Graph** – the system of record for profiles and connections; transactional writes.
* **Analytics** – CPU/memory-heavy derived computations (O(V·E) betweenness); read-only.

## Decision
Three C++ services, each owning its data, plus an nginx gateway:
`identity-service` (DB `identity`), `graph-service` (DB `graph`), `analytics-service` (no DB; derived
in-memory state). No service reads another's database. State propagates through events
(ADR-0004); the only synchronous service-to-service call is analytics bootstrapping from
graph-service's snapshot endpoint. Tokens are verified locally (ADR-0005), so identity is never on
the request path of other services.

## Alternatives
* **Modular monolith** – simpler to run and debug, one transaction boundary; rejected per the owner's
  requirement, but the internal layering (domain/app/infra/http) keeps each service small enough that
  merging them back would be mechanical.
* **Finer split** (separate recommendation or community services) – they share one in-memory graph
  and the same version; splitting would duplicate the read model for no isolation benefit.

## Consequences
* (+) Analytics can be scaled and resourced independently (more cores, more replicas) without touching
  the write path; a slow betweenness computation cannot block logins or connection writes.
* (+) Each service's blast radius and schema are small; identity has no access to graph data.
* (−) Eventual consistency between services: a new account's profile appears in graph-service a few
  milliseconds after registration; analytics lags writes by the event-propagation delay. Mitigated by
  `graph_version` + `min_version` read-your-writes (contracts §6) and profile-provisioning handling in the UI.
* (−) More moving parts: Redis, outbox relays, three deployables, distributed failure modes. Covered by
  health/readiness checks, idempotent consumers, gap detection with resync, and end-to-end tests.
