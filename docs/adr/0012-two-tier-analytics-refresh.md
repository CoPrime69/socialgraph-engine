# ADR-0012: Two-tier analytics refresh

## Context
analytics-service rebuilds its snapshot when connection events arrive (ADR-0006). The global
scores (PageRank, betweenness, closeness, Louvain communities) cost O(k·E) to O(V·E) and took
933 ms on the full dataset; under steady writes the service recomputed them about once a second,
keeping CPU cores busy that should have served requests on the same machine.

## Decision
Two tiers, configured by `ANALYTICS_MIN_REFRESH_MS` (default 1 s) and `ANALYTICS_FULL_REFRESH_MS`
(default 30 s):
* An **incremental refresh** rebuilds the graph structure (CSR adjacency), degrees, components and
  rankings, and carries PageRank, betweenness, closeness and community membership over by user id.
  New members start at 0 and join the community of their strongest neighbour.
* A **full refresh** recomputes everything, at most once per `ANALYTICS_FULL_REFRESH_MS`, and also
  when the interval has passed with no further writes, so a carried snapshot is always upgraded.
* The summary endpoint exposes both: `graph_version` (structure) and `centrality_graph_version` /
  `centrality_computed_at` (when the global scores were last computed), and the ETag includes both.

## Alternatives
* **Recompute everything on every change** – exact but the dominant CPU cost under writes.
* **Incremental PageRank/betweenness algorithms** – exist, but are complex, approximate in practice,
  and would still need periodic full recomputation to bound drift.
* **A separate batch job** – adds a scheduler and storage for results; the in-process timer gives
  the same effect with less machinery.

## Consequences
* (+) The expensive computation runs at most every 30 s instead of about every second under load.
* (+) Structure-dependent answers (connections, paths, degrees, recommendations) still follow
  writes within about a second.
* (−) Global scores can be up to 30 s behind the graph; clients can see by how much from
  `centrality_graph_version`.
