# libs/graphcore (`sg::graph`)

Pure C++20 graph algorithms for the platform: no I/O, no services, no global mutable state.
graph-service and analytics-service link the static library `sg_graphcore` (alias
`sg::graphcore`, PUBLIC include dir `include/`, links `Threads::Threads`) and include
`<sg/graph/graphcore.hpp>` or the individual headers below. Domain formulas follow
`docs/contracts.md` §8 and §10. Measured performance is in
[`docs/performance-graphcore.md`](../../docs/performance-graphcore.md).

## Contents

| Header (`include/sg/graph/`) | Provides |
|---|---|
| `csr_graph.hpp` | `NodeId`, `WeightedEdge`, `CsrGraph`: immutable undirected weighted graph in CSR form (sorted neighbour lists, no duplicates, no self-loops, symmetric weights) |
| `affinity.hpp` | `Profile`, `affinity`, `connection_strength`, `louvain_weight` |
| `centrality.hpp` | `pagerank`, `betweenness` (exact or pivot-sampled, multi-threaded), `closeness`, `degree_centrality` |
| `community.hpp` | `louvain`, `modularity` |
| `components.hpp` | `connected_components` |
| `paths.hpp` | `shortest_path` (bidirectional BFS), `bfs_distances` |
| `recommend.hpp` | `recommend`: Adamic–Adar + affinity + same-community score |
| `generator.hpp` | `SplitMix64` (portable PRNG, Lemire bounded draws), `generate_homophilous_edges` |
| `parallel.hpp` | `parallel_for(begin, end, threads, body(i, worker))`, `resolve_thread_count` |
| `graphcore.hpp` | umbrella header |

`src/` holds the implementations, `tests/` the GoogleTest suite (`graphcore_tests`), `bench/`
the Google Benchmark suite (`graphcore_bench`) with its dataset loader and the legacy baseline.

## Algorithms

V = nodes, E = undirected edges, k = sources or iterations, t = threads.

| Function | Method | Time | Extra memory |
|---|---|---|---|
| `CsrGraph::from_edges` | counting sort by source, per-list sort only when unsorted, keep first duplicate | O(V + E log d_max), O(V + E) for sorted input | O(V + E) |
| `pagerank` | power iteration, uniform dangling mass, stop when L1 change ≤ tolerance | O(k (V + E)) | O(V) |
| `betweenness` | Brandes 2001; predecessors recognised by `dist[v] == dist[w] − 1` (no lists); fixed source blocks merged in block order, so results are bit-identical for any thread count; optional pivot sampling scaled by n/k | O(k (V + E)), exact O(V E) | O(t V) + block buffers |
| `closeness` | bit-parallel BFS, 64 sources per sweep, bit-sliced distance counters; Wasserman–Faust for disconnected graphs | O(⌈V/64⌉ L (V + E)), L ≤ diameter + 1 | O(t V) |
| `degree_centrality` | deg/(n − 1) | O(V) | O(V) |
| `louvain` | local moving in ascending id order (strict gain), aggregation; final modularity recomputed on the input graph | O(p (V + E)) per level | O(V + E) |
| `modularity` | Newman–Girvan with resolution γ | O(V + E) | O(V) |
| `connected_components` | BFS | O(V + E) | O(V) |
| `shortest_path` | bidirectional BFS, smaller frontier first, per-thread stamped scratch | O(b^(d/2)) typical, O(V + E) worst | O(V) per thread, reused |
| `bfs_distances` | BFS | O(V + E) | O(V) |
| `recommend` | Adamic–Adar over the 2-hop neighbourhood + affinity + community, bounded top-k heap, per-thread scratch reset in O(touched) | O(V log limit + Σ_{z ∈ N(u)} deg z) | O(V) per thread, reused |
| `generate_homophilous_edges` | SplitMix64 draws inside/outside the group, sorted and deduplicated | O(V d log(V d)) | O(V d) |

Conventions worth knowing:

* **Determinism.** Every result is a pure function of its inputs (and seed). Betweenness and
  closeness are bit-identical for every thread count; Louvain is deterministic and numbers
  communities by size (descending), ties by smallest member.
* **Louvain self-loops.** An aggregated node's internal weight is a self-loop counted once in the
  total weight m and twice in the node strength, so Σ k_i = 2m (Newman's convention).
* **Thread safety.** `CsrGraph` is immutable after construction and can be read concurrently.
  All functions are reentrant; `shortest_path` and `recommend` keep their scratch in
  `thread_local` storage (sized to the largest graph seen on that thread).
* **Errors.** Invalid arguments throw `std::invalid_argument`, out-of-range node ids
  `std::out_of_range`. Edge weights must be finite (and ≥ 0 for Louvain).

## Build, test, benchmark

From the repository root (this builds only graphcore; drop `-DSG_BUILD_SERVICES=OFF` to build
everything):

```sh
cmake -S . -B build/gc -G Ninja -DCMAKE_BUILD_TYPE=Release -DSG_BUILD_SERVICES=OFF \
      -DSG_BUILD_BENCHMARKS=ON -DSG_WARNINGS_AS_ERRORS=ON
cmake --build build/gc
ctest --test-dir build/gc --output-on-failure

# sanitizers
cmake -S . -B build/gc-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSG_BUILD_SERVICES=OFF \
      -DSG_BUILD_BENCHMARKS=OFF -DSG_SANITIZE=address,undefined
cmake -S . -B build/gc-tsan -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DSG_BUILD_SERVICES=OFF \
      -DSG_BUILD_BENCHMARKS=OFF -DSG_SANITIZE=thread

# benchmarks (needs libbenchmark-dev; zlib for the real dataset)
./build/gc/libs/graphcore/bench/graphcore_bench --stats             # graph statistics
./build/gc/libs/graphcore/bench/graphcore_bench                     # all benchmarks
./build/gc/libs/graphcore/bench/graphcore_bench --benchmark_filter=so2008
```

The benchmark reads `data/stackoverflow/{users,connections}.csv.gz` (plus `categories` and
`branches` for the taxonomy codes) by streaming them through zlib; set `SG_DATASET_DIR` to read
another directory. Without zlib it reads uncompressed `.csv` files of the same names, and
benchmarks of the real graph are reported as skipped when neither is present.
