# ADR-0010: In-process read caches, invalidated by events

## Context
Most requests are reads, and most reads ask for something that has not changed since the last
time someone asked: a popular profile, a tag's question list, a member's activity. Answering them
from PostgreSQL costs a pooled connection, a round trip and JSON rendering each time. Under the
mixed load test ([performance.md](../performance.md)) PostgreSQL was the largest CPU consumer once
the HTTP layer stopped being the limit.

What may be cached, and for how long, differs by service:
* **content** data is imported once and never changes;
* **analytics** answers are derived from an immutable, versioned snapshot (ADR-0006);
* **graph** profiles and connection lists change on every connection write, and several replicas
  of graph-service may serve the same member.

## Decision
Each service keeps rendered response bodies in process memory, in front of its source of truth
(cache-aside), in `sg::common::LruCache`: a size-bounded LRU split into 16 shards by key hash, so
concurrent requests rarely contend on one lock.

* **content-service** caches every read by its canonical parameters, with a strong ETag derived from
  the body, so revalidating clients get 304 (`CONTENT_CACHE_ENTRIES`, default 50,000). No
  invalidation is needed because the data is immutable.
* **analytics-service** caches serialized responses per snapshot; a new graph version starts a new
  set. ETags carry the graph version and the version of the expensive scores (ADR-0012).
* **graph-service** (`ReadCache`) caches each member's profile and connection list, invalidated by
  member id:
  * by the replica that made a write, as soon as it commits (read-your-writes for that member);
  * by every replica when the matching `connection.*` or `user.created` event arrives on
    `graph.events` (`GraphEventsInvalidator`, a plain `XREAD` without a consumer group, so every
    replica sees every event). It starts reading a few seconds in the past so nothing is missed at
    start-up, and clears the whole cache after a Redis error, when events may have been missed.
  * A load that started before an invalidation is never stored after it: a striped generation
    counter is read before the database call and checked under the shard lock when storing
    (`LruCache::put_if`), and `invalidate()` bumps it before erasing.
* **Member search** pages are cached as the ordered ids they list plus the total, for
  `GRAPH_SEARCH_CACHE_TTL_MS` (default 30 s). Each request renders the page from the members' cached
  profiles, so the connection counts it shows are as fresh as the profile cache; only *which*
  members match is reused, and that changes only when members join.
* **Token verification** caches verified tokens by their exact text (100,000 entries). Expiry and
  not-before are re-checked on every hit, so a cached token stops working on time.
* Cached reads are answered on the HTTP event loop; only a miss moves to the worker pool
  (ADR-0002).

## Alternatives
* **Redis as a shared cache**: one copy for all replicas, but every hit becomes a network round
  trip and a deserialisation, which is most of what a hit is meant to avoid. Coherence via events
  works the same either way. Worth revisiting if memory per replica becomes the limit.
* **Time-based expiry for member data**: simpler, but a member who connects to someone would see
  their old connection list until it expires. Event invalidation gives read-your-writes on the
  replica that wrote and second-scale staleness elsewhere.
* **Invalidating search pages per member**: a page lists up to 100 members, so every write would
  have to find all pages containing either member. Caching ids and hydrating from member entries
  gives the same freshness for counts without that index.
* **HTTP caching in nginx**: responses are per user (Authorization), so a shared proxy cache would
  need careful keying; the services already know exactly when their data changes.

## Consequences
* (+) On the mixed workload, cache hits skip PostgreSQL and JSON rendering entirely; together with
  the other changes in performance.md, throughput went from 868 to 6,554 requests/s.
* (+) Replicas need no coordination beyond the events they already publish.
* (−) Another replica's cache is stale for as long as the event takes to arrive (typically
  milliseconds; the outbox relay and Redis are on the path). New members can take up to the search
  TTL to appear in search results.
* (−) Memory per replica grows with the cache sizes; all limits are configurable and 0 disables a
  cache.
