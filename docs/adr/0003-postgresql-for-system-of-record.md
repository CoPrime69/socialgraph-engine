# ADR-0003: PostgreSQL as the system of record

## Context
The original program kept users in a CSV (with plaintext passwords) and an n×n adjacency matrix CSV
rewritten in full on every change; the in-memory connection list was regenerated randomly on every
start, so state was never actually persisted consistently (docs/audit.md §3–4).

## Decision
PostgreSQL 16, accessed through libpqxx with a small bounded connection pool (`sg::common::PgPool`),
parameterized queries only, per-connection `statement_timeout`, and versioned SQL migrations embedded
in each binary and applied under an advisory lock at start-up.

* identity DB: `accounts` (unique handle, Argon2id hash), `outbox`.
* graph DB: `profiles` (taxonomy CHECK constraints, influence 0–100), `connections`
  (`user_a < user_b` CHECK, composite PK prevents duplicates, FK cascades, index on `user_b`),
  `graph_meta` (gap-free version counter), `outbox`, `processed_events` (consumer idempotency).

## Alternatives
* **Keep files / SQLite** – no concurrent writers across processes, no network access for separate
  services, weaker constraints.
* **A graph database (Neo4j)** – attractive for traversal queries, but our algorithms run on an
  in-memory CSR snapshot anyway; the store only needs reliable transactional writes and simple reads.

## Consequences
* Integrity rules (no self-connections, no duplicates, valid taxonomy) are enforced by the database
  as well as by validation, so a bug in one layer cannot corrupt data.
* Writes that must also emit an event do so in the same transaction (ADR-0004).
