# Performance: measured throughput of the whole application

This document records how fast the complete system serves a realistic mix of user traffic, how
that number was found, and what changed it. Every figure here comes from a run on the machine
described below; nothing is extrapolated. Algorithm micro-benchmarks of `libs/graphcore` are in
[performance-graphcore.md](performance-graphcore.md).

## Headline

| Run (saved in `tools/loadtest/results/`) | Throughput | p50 | p90 | p99 | Errors |
|---|---:|---:|---:|---:|---:|
| `baseline-httplib`: first complete build | 868 req/s | 10.4 ms | 88.1 ms | 1,390.6 ms | 0 |
| `httplib-graph-cache`: read cache on graph-service only | 742 req/s | 9.8 ms | 299.3 ms | 5,025.8 ms | 0 |
| `httplib-320-threads`: 320 HTTP threads per service | 1,096 req/s | 69.7 ms | 214.4 ms | 1,142.8 ms | 0.067 % (504s) |
| `httplib-320-all-opts`: group commit, autocommit reads, two-tier analytics | 1,289 req/s | 78.3 ms | 208.1 ms | 367.1 ms | 0 |
| `drogon-all-opts`: everything below | **6,554 req/s** | **16.6 ms** | **36.8 ms** | **64.6 ms** | **0** |

The same workload, protocol and machine gave 7.5 times the throughput and a p99 21 times lower
than the first build, with no errors.

## What is measured

**Machine.** A cloud VM with 4 vCPUs (Intel Xeon @ 2.80 GHz) and 16 GB RAM. All of the following
share those 4 vCPUs: the four C++ services, PostgreSQL 16, Redis, the nginx gateway (Docker, host
network), and the load generator itself.

**Topology.** Requests go through the gateway exactly as the browser's do:
`autocannon → nginx :8080 → identity | graph | analytics | content`. The services run as native
Release binaries started by [`tools/loadtest/local-stack.sh`](../tools/loadtest/local-stack.sh),
which recreates and seeds every database from `data/stackoverflow` on each start, so every run
begins from the same state.

**Workload.** [`tools/loadtest/workload.mjs`](../tools/loadtest/workload.mjs) signs up 200 members,
gives each a few connections, then keeps 2 autocannon processes × 64 keep-alive connections busy
for 30 s (after 5 s of warm-up), as fast as the system answers. Every request is drawn at random
from the weighted mix in [`lib/mix.mjs`](../tools/loadtest/lib/mix.mjs) for a random member, with
ids and search terms sampled from the real dataset (profiles and questions weighted by the square
root of their view counts, tags by question count, search terms taken from real names and titles):

| Area | Operations (share of requests) |
|---|---|
| Graph | view profile 14 %, search members 9 %, my connections 7 %, connect 4 %, disconnect 2 % |
| Analytics | influencers 8 %, recommendations 8 %, degrees of separation 5 %, member metrics 5 %, network summary 3 %, ego network graph 3 %, communities 2 % |
| Content | member activity 9 %, questions by tag 8 %, view question 6 %, search questions 5 %, browse tags 2 % |

6 % of requests are writes (connect, disconnect), which also flow through the outbox and Redis
Streams into analytics and the other replicas' caches while the test runs.

**Reproduce.**

```bash
cmake --preset release && cmake --build --preset release
eval "$(BUILD_DIR=build/release tools/loadtest/local-stack.sh start | sed 's/^/export /')"
LABEL=my-run node tools/loadtest/workload.mjs   # writes results/<time>-my-run.{json,md}
tools/loadtest/local-stack.sh stop
```

## How the number moved

The method was the same at every step: run the workload, find where the time goes (per-process
CPU with `top` and `perf record -a`, active statements sampled from `pg_stat_activity`, `EXPLAIN
ANALYZE` of the hot statements), change one thing, run again. "Probe" numbers below come from
shorter runs (20-25 s, not saved) used to compare variants on the same build; the saved runs above
are the reference.

### 1. The first build: threads waiting, not working (868 req/s)

cpp-httplib gives each keep-alive connection its own thread for as long as the connection lives.
With 128 load-generator connections spread over four services and 16 threads each, most
connections waited for a thread, which showed up as a p99 of 1.4 s and stalls of several seconds.
Adding a read cache to graph-service alone made it worse (742 req/s, p99 5 s): faster handlers do
not help when connections cannot get a thread. 320 threads per service removed the stalls but not
the waiting (1,096 req/s, 504s from the gateway).

### 2. Database and analytics work (1,289 req/s saved; 1,928 req/s probe)

* **Group commit of connection writes.** One writer thread drains the queue and applies up to 512
  writes in one transaction (bulk `INSERT … SELECT FROM unnest`, bulk outbox insert, one version
  update), so concurrent writes share one fsync instead of queueing on the version row. Versions
  and events stay gap-free and in arrival order. Writes probe with 64 concurrent writers: 216 →
  508 → 750 writes/s, p99 1,125 → 217 ms.
* **Autocommit reads.** Single-statement reads skip `BEGIN`/`COMMIT` (`PgPool::with_read`).
* **Two-tier analytics refresh.** PageRank, betweenness, closeness and Louvain take 933 ms on the
  full graph and were recomputed about once a second under writes. Refreshes now rebuild the graph
  structure and cheap metrics and carry the expensive scores over by user id; a full computation
  runs at most every 30 s (`ANALYTICS_FULL_REFRESH_MS`).
* **Denormalisation and indexes**, each checked with `EXPLAIN ANALYZE` on a freshly seeded database:

  | Statement | Before | After | Change |
  |---|---:|---:|---|
  | member activity totals | 7.7 ms | primary-key lookup | `user_activity` precomputed at import |
  | member's top tags | 2.2 ms | 0.12 ms | index `user_tags (user_id, posts DESC, score DESC)` |
  | rare tag, questions page | 7.2 ms | 0.14 ms | score and date copied onto `question_tags`, (tag, key) indexes |
  | popular tag, question count | 26 ms | 0.67 ms | count on `question_tags` without the join |
  | member search count | 24 ms | 0.55 ms | generated lower-case columns with trigram GIN indexes, `connection_count` kept by triggers |

  Two import-time fixes made the indexes usable from the first request: GIN indexes are created
  with `fastupdate = off`, and every import ends with `VACUUM (ANALYZE)`.

After these, per-request work was small, and the HTTP layer was again the limit: system (kernel)
time was 22 % of all CPU, most of it switching between hundreds of threads.

### 3. Event-loop HTTP server (Drogon): 3,767-4,349 req/s probe

[ADR-0002](adr/0002-cpp20-and-drogon.md) moved `sg::common::HttpServer` to Drogon. A few event-loop
threads per process multiplex all connections; handlers that may block run on a fixed worker
pool. The library's API did not change, so services only changed their wiring. With the thread
problem gone, `perf` showed PostgreSQL using 43 % of all CPU, mostly parsing and planning.

### 4. Prepared statements and one round trip per read: about 5,200 req/s probe

* `sg::common::exec_cached()` prepares each statement once per pooled connection and reuses it.
  Statements that used catch-all filters (`$1 IS NULL OR …`) are built per filter combination so a
  cached plan fits the real predicate.
* Member search and connection lists return the page together with its total (or graph version)
  from one statement, so they need neither a transaction nor five round trips.

### 5. Cache hits on the event loop, and mimalloc

* Handlers of graph-service and content-service now run on the I/O thread first. A cache hit is
  answered there; a miss calls `sg::common::require_worker()` and the request moves to the worker
  pool before touching PostgreSQL. Writes start on the pool.
* `perf` showed `malloc` and `_int_malloc` near the top of the worker threads. Same build, three
  allocators, fresh stack each, two 25 s runs after a warm-up run:

  | Allocator | Run 1 | Run 2 | Mean |
  |---|---:|---:|---:|
  | glibc malloc | 5,871 | 5,533 | 5,702 req/s |
  | jemalloc 5.3 | 5,808 | 6,360 | 6,084 req/s |
  | mimalloc 2.1 | 6,260 | 6,598 | 6,429 req/s |

  The services now link mimalloc ([`cmake/Allocator.cmake`](../cmake/Allocator.cmake)); sanitizer
  builds keep the system allocator.

### 6. Member search as cached id lists

Search was the most frequent statement left in PostgreSQL. A search page is now cached as the
member ids it lists plus its total, and each request renders it from the members' cached profiles
(the id-list-then-hydrate pattern of feed and search caches). Connection counts in results are as
fresh as the profile cache, which writes invalidate; which members match is reused for
`GRAPH_SEARCH_CACHE_TTL_MS` (30 s), so a new member appears in searches within 30 s. Three runs
after a warm-up: 7,056, 6,521 and 6,150 req/s.

## Where the CPU goes now

`perf record -a` during the mixed workload, before the search change (share of all CPU samples):

| Process | Share |
|---|---:|
| PostgreSQL backends | 32 % |
| Load generator (2 × Node.js autocannon) | 18 % |
| Drogon I/O threads (all services) | 16 % |
| Worker threads (graph, content, identity) | 14 % |
| nginx gateway | 10 % |
| analytics refresh thread | 7 % |

About a quarter of the machine runs the load generator and the gateway rather than the
application. The heaviest remaining statement is the member's own connection list: members in the
test connect and disconnect constantly, so their lists are re-read after almost every write, and
they grow over a run. That growth is also why back-to-back runs on one stack get slower (all
operations slow down evenly, 7,056 → 6,150 req/s over three runs); each saved run starts from a
fresh stack.

## Why not 500,000 requests per second here

On this machine every request costs about 600 µs of CPU in total (4 vCPUs fully busy at
~6,500 req/s), including the load generator, the gateway, PostgreSQL and the analytics refresh.
500k req/s would need roughly 75 times this box's CPU at the same cost per request, so it is not
reachable on one 4-vCPU VM by tuning alone. The design scales out instead of up:

* graph, content and analytics are stateless behind the gateway, and their caches stay coherent
  across replicas through `graph.events` (graph) or because the data is immutable (content);
* analytics replicas each hold the whole read model and need no database;
* PostgreSQL reads can move to replicas, and the hot read paths already avoid the database on a
  cache hit.

Measuring that scale-out needs more cores than this VM has; it is the next experiment.

## Caveats

* A shared 4-vCPU VM is noisy: two runs of the same build differed by up to 10 %. Comparisons above
  use runs taken back to back on the same build and fresh stack.
* PostgreSQL runs with its default durability (`synchronous_commit = on`); every acknowledged
  write is on disk.
* The load test reaches the services through one gateway on the same host, so network latency is
  not part of these numbers.
