# ADR-0004: Redis Streams with a transactional outbox

## Context
Services must learn about each other's changes without sharing databases: graph-service needs to
create a profile when an account is registered; analytics-service needs every graph change, in order.
Losing or reordering events would silently corrupt the analytics read model.

## Decision
* **Transactional outbox**: a producer writes its state change and an `outbox` row in one PostgreSQL
  transaction. A relay thread (single leader per database via `pg_try_advisory_lock`) publishes
  unpublished rows in id order with `XADD` and marks them published. Delivery is at-least-once.
* **Redis Streams** as the bus: `identity.events` and `graph.events`.
  * graph-service consumes `identity.events` through a consumer group (`XREADGROUP`/`XACK` after its
    own transaction commits), with a `processed_events` table making the handler idempotent.
  * analytics-service reads `graph.events` with plain `XREAD` so every replica sees every event.
* **Ordering**: every graph write increments `graph_meta.version` inside its transaction. The row lock
  makes versions gap-free and commit-ordered. The analytics read model applies version `v+1` only; a
  duplicate is skipped and a gap triggers a resync from the snapshot endpoint.

## Alternatives
* **Synchronous HTTP calls** (identity → graph on register, graph → analytics on change), which couples
  availability (registration fails if graph is down) and loses changes on partial failure.
* **Kafka**: the right tool at large scale, but heavy to operate for this workload.
* **NATS JetStream / RabbitMQ**: good fits; Redis was chosen because hiredis is a small, stable,
  distro-packaged C client and Streams provide ordering, consumer groups and replay.
* **PostgreSQL LISTEN/NOTIFY**: notifications are lost while a consumer is disconnected.

## Consequences
* (+) No dual-write problem: an event exists if and only if its transaction committed.
* (+) Consumers recover after downtime by replaying from their last position.
* (−) Streams grow without trimming in this version; trimming is safe for analytics (gap ⇒ resync)
  and for graph-service once entries are acknowledged; this is listed as future work.
* (−) Redis must be persistent (`appendonly yes` in Compose) to avoid losing unconsumed events.
