# ADR-0011: Group commit of connection writes

## Context
Every connect or disconnect must, atomically: change `connections`, bump the global
`graph_meta.version`, and enqueue a versioned event in the outbox (ADR-0004), so that consumers see
gap-free versions in commit order. That single version row serialises all writers. With one
transaction per request, concurrent writers queued on the row lock and each paid its own WAL
flush; a probe with 64 concurrent writers reached 216 writes/s with a p99 of 1,125 ms.

## Decision
`PostgresGraphRepository` hands each write to one writer thread (`GroupCommitWriter`):
* Requests enqueue `{add|remove, a, b, strength}` and wait on a future.
* The writer drains the queue (up to `max_batch` = 512) into one transaction: lock the version row,
  apply all inserts with one `INSERT … SELECT FROM unnest(...) ON CONFLICT DO NOTHING`, all deletes
  with one `DELETE … USING unnest(...)`, enqueue all events with one bulk outbox insert, and update
  the version once.
* Within a batch, at most one write per member pair is taken; a later write to the same pair waits
  for the next batch, so each request's outcome (created, already connected, removed, not found)
  is exactly what it would be if the writes ran one by one in arrival order.
* Versions and events are assigned in arrival order, so consumers still see one version per change,
  without gaps.
* Profile existence is checked before enqueueing (profiles are never deleted), so a batch never
  contains a statement that can fail on its own.

## Alternatives
* **Per-request transactions with `synchronous_commit = off`**: faster, but an acknowledged write
  can be lost on a crash, and the version row still serialises writers.
* **Sequence instead of a version row**: sequences are not transactional, so versions would have
  gaps on rollback and consumers could not detect lost events from gaps.
* **Sharding the version per member**: consumers rely on one global, ordered version to know a
  snapshot is complete; per-member versions would need a different event protocol.

## Consequences
* (+) Probe with 64 concurrent writers: 216 → 750 writes/s, p99 1,125 → 217 ms, with full
  durability.
* (+) A single writer makes the ordering argument trivial to reason about and to test
  (`ConcurrentWritesToOnePairStayConsistentUnderGroupCommit`).
* (−) Throughput of writes per graph-service *database* is bounded by one writer thread; scaling
  writes further means partitioning the graph, not adding replicas.
* (−) A failing batch (the database going away, a timeout) fails every request in it with the same
  error. Single writes cannot fail on their own, because profiles are checked before enqueueing and
  conflicts are absorbed by `ON CONFLICT` and `RETURNING`.
