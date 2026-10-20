# ADR-0007: Louvain modularity optimisation for community detection

## Context
The legacy "community detection" sorted users by branch/category and cut the list into random-sized
chunks before a heuristic refinement; results were random on every run and had no quality measure.

## Decision
Use the **Louvain** method (local moving + aggregation) maximising weighted modularity, with
deterministic node order and tie-breaking. Edge weight = `0.5 + 0.5·strength`, so network structure
dominates while attribute affinity (category, branch and influence: the legacy intent) refines the
partition. The reported modularity is recomputed on the original graph.

## Alternatives
* **Label propagation**: near-linear and simple, but non-deterministic without extra care and gives
  no objective to report.
* **Leiden**: fixes Louvain's badly-connected communities; more complex. A natural upgrade if
  disconnected communities are observed (Louvain's known limitation, noted in docs).
* **Attribute grouping only**: would ignore the graph entirely.

## Consequences
* Same input ⇒ same communities, which makes tests and the UI stable.
* Modularity is surfaced in the API/UI as a quality signal.
