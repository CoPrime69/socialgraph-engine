# ADR-0006: Analytics as an in-memory, versioned CQRS read model

## Context
Centrality and community algorithms need the whole graph. Betweenness is O(V·E). Recomputing per
request is wasteful; persisting results would add a schema that duplicates derived data.

## Decision
analytics-service keeps the graph in memory, built from graph-service's snapshot and kept current by
`graph.events` (ADR-0004). For each graph version it computes, once, a CSR graph plus PageRank,
betweenness, closeness, degree, Louvain communities, connected components and per-metric rankings.
The result is immutable and shared via `std::atomic<std::shared_ptr<const Analytics>>`:

* readers of the current version do a lock-free load;
* the first reader after a change computes under a mutex (single flight), or the event consumer
  computes eagerly after each batch so readers rarely wait;
* readers holding an older snapshot keep a consistent view until they finish.

Above 5,000 nodes betweenness switches to deterministic pivot sampling (configurable), reported as
`betweenness_approximate` in the summary. Clients that just wrote can pass `min_version` to wait (≤ 2 s)
for their write to be reflected.

## Alternatives
* **Cache results in Redis** – adds serialisation and staleness bookkeeping without saving computation;
  each replica would still need the graph for recommendations and paths.
* **Incremental algorithms** – dynamic betweenness/PageRank are complex and error-prone; full
  recomputation is a few milliseconds at the seed size and is measured in `docs/performance.md`.

## Consequences
* Memory is O(V+E) per live version; at most two versions are alive briefly during a swap.
* Throughput of reads is independent of algorithm cost; write bursts are coalesced because a
  computation always covers the latest version available when it starts.
