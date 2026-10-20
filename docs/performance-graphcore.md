# graphcore performance

Measured performance of `libs/graphcore` (`sg::graph`) on the real Stack Overflow 2008 interaction
graph and on synthetic graphs of 1k, 10k and 100k nodes, compared with a faithful re-implementation
of the legacy C++ code (`legacy/cpp files/connection_manager.cpp`). Every number below was measured
on 2026-10-19 on the machine in §3 with the commands in §4; nothing is estimated except where a
value is explicitly marked **≈ (extrapolated)**, and §8 says how those were derived.

Benchmark sources: `libs/graphcore/bench/` (`graphcore_bench.cpp`, `dataset.cpp` for the loader,
`legacy_baseline.cpp` for the legacy code, `run_isolated.py` for the runner).

## 1. Headline: the real graph (Stack Overflow 2008, 18,648 users, 204,650 connections)

Median of 3 repetitions, wall-clock time. "Legacy" is a faithful port of the original string-keyed
implementation (`bench/legacy_baseline.cpp`), single-threaded as it ran (its build never enabled
OpenMP).

| Operation | sg::graph | Legacy | Speed-up |
|---|---:|---:|---:|
| Load `users.csv.gz` + `connections.csv.gz` (zlib streaming, CSV parse, CSR build) | 171 ms | — | — |
| Build the in-memory graph from parsed rows | 9.24 ms | 24.6 ms | 2.7× |
| PageRank, one iteration | 504 µs | 54.3 ms | 108× |
| PageRank, full run | 22.7 ms (45 iterations to L1 change ≤ 1e-10) | 217 ms (4 iterations, stops unconverged) | — |
| Betweenness, cost per source, 1 thread | 3.28 ms | 138 ms | 42× |
| Betweenness, cost per source, 4 threads | 0.907 ms | 138 ms (1 thread) | 153× |
| **Betweenness, exact, all 18,648 sources** | **16.9 s** (4 threads); ≈ 61 s (1 thread, extrapolated) | ≈ 43 min (extrapolated) | ≈ 153× |
| Betweenness, 256 sampled pivots | 839 ms (1 thread), 243 ms (4 threads) | — | — |
| **Closeness, all 18,648 sources** | **771 ms** (1 thread), **187 ms** (4 threads) | ≈ 21 min (extrapolated), and every value is +∞ | 1,621× (1 thread) |
| **Shortest path ("degrees of separation"), one query** | **0.95 µs** | 50.7 ms | 53,290× |
| Louvain communities | 59.5 ms → 42 communities, modularity 0.3231 | no graph-based equivalent | — |
| Recommendations for one user (top 10) | 267 µs | not comparable | — |
| BFS distances from one source | 828 µs | — | — |
| Connected components | 890 µs | — | — |

What the ratios mean: PageRank and betweenness run the same algorithm on the same graph on both
sides, so their ratios are the cost of string-keyed hash maps and per-node heap objects versus
contiguous arrays (plus threads for the 4-thread row). Closeness and shortest path also change the
algorithm: 64 BFS sources per sweep (bit-parallel) instead of one, and a bidirectional search that
stops when the two frontiers meet instead of a full BFS of the component on every query.

## 2. The dataset

`data/stackoverflow/` (contracts.md §10; built reproducibly by
`tools/dataset/build_stackoverflow_dataset.py`, CC BY-SA 4.0): the 18,648 people who interacted on
Stack Overflow between its launch in August 2008 and 31 December 2008; a connection is a real
interaction (one answered or commented on the other's post), aggregated per pair.

The benchmark reads the files itself, without shelling out: `users.csv.gz` and
`connections.csv.gz` are streamed through zlib (`gzopen`/`gzread`, 256 KiB buffer, 1 MiB read
buffer) into a streaming RFC 4180 parser (quoted fields, doubled quotes, embedded line breaks,
CRLF), columns are looked up by header name, and numbers are parsed with `std::from_chars`.
`categories.csv.gz` and `branches.csv.gz` give the `Profile` codes (row order = display order,
§10.3); the connection `strength` is the edge weight and `influence` the profile influence.
16.4 MB of CSV (3.9 MB users + 12.5 MB connections, uncompressed) load in 171 ms including the CSR
build. If CMake does not find zlib, `graphcore_bench` is built without it (a configure message says
so) and reads uncompressed `users.csv` / `connections.csv` from the same directory instead
(`gunzip -k data/stackoverflow/*.csv.gz`); when neither form is present the real-graph benchmarks are
reported as skipped with the reason. `SG_DATASET_DIR` overrides the directory.

The loaded graph matches `manifest.json` exactly: 18,648 nodes and 204,650 edges, i.e. every row of
`connections.csv` became one distinct undirected edge (no duplicates, no self-loops).

Statistics computed by `graphcore_bench --stats` (the interaction counts by a short Python pass over
`connections.csv.gz`):

| Property | Value |
|---|---|
| Nodes / edges | 18,648 / 204,650 (density 0.00118) |
| Degree | min 1, median 7, **mean 21.95**, p90 54, p99 215, **max 1,360** — heavy-tailed |
| Degree distribution | 1: 2,581 nodes · 2: 1,836 · 3–4: 2,520 · 5–8: 3,063 · 9–16: 2,987 · 17–32: 2,452 · 33–64: 1,738 · 65–128: 970 · 129–256: 375 · 257–512: 104 · 513–1,024: 20 · 1,025–2,048: 2 |
| Isolated nodes | 0 |
| Connected components | 5; the largest holds 18,640 nodes (99.96 %) |
| Distances (BFS from 256 random sources) | mean 3.29 hops, largest seen 7 |
| Interactions per connection | min 1, median 1, mean 1.28, max 73; 81.1 % of pairs interacted once |
| Edge weight (`strength`) | min 0.142, mean 0.396, max 1.000 |
| Louvain (weights 0.5 + 0.5·strength) | 42 communities, modularity 0.3231, 3 levels |
| PageRank | 45 iterations; top 5 are user ids 22656 (degree 1,360), 3043 (1,111), 12950 (936), 1965 (884), 23354 (828) |

Results on this graph were cross-checked once (outside the repository's test suite, which covers
the same functions on small graphs) against NetworkX 3.7 and independent Python code. PageRank
differed from `networkx.pagerank` by at most 1.3e-11. Closeness of 200 random nodes matched a plain
BFS computation exactly. Betweenness from 64 pivots matched a Python Brandes run on the same pivots
to 2e-15 relative. The Louvain modularity recomputed from our partition was identical (0.3230965965),
and NetworkX's own `louvain_communities` (random node order, seed 1) reached Q = 0.3256 with 23
communities on the same weights. Components matched, as did 300 random shortest paths (valid edges,
BFS length) and the top-10 recommendations of 8 users (brute force of the contract formula).

The synthetic graphs (`make_synthetic(n)` in `bench/dataset.cpp`) put n nodes into ⌊n/32⌋ random
groups (branch = group mod 73, category = branch mod 10, mirroring the 73 branches and 10
categories of the real taxonomy; influence uniform on 0–100), draw edges with
`generate_homophilous_edges` (average degree 10, μ = 0.2) and weight them with
`connection_strength`. Unlike the real graph their degrees are narrow and their node ids are
random with respect to structure:

| Graph | Nodes | Edges | Degree mean / max | Components | Mean distance (max seen) | Louvain |
|---|---:|---:|---|---:|---|---|
| synthetic 1k | 1,000 | 4,579 | 9.16 / 16 | 1 | 3.83 (6) | 31 communities, Q = 0.8166 |
| synthetic 10k | 10,000 | 45,656 | 9.13 / 19 | 1 | 5.19 (8) | 141 communities, Q = 0.8445 |
| synthetic 100k | 100,000 | 456,741 | 9.13 / 21 | 1 | 6.52 (9) | 410 communities, Q = 0.8513 |

## 3. Machine, toolchain and build

| | |
|---|---|
| CPU | `Intel(R) Xeon(R) Processor @ 2.80GHz` (`/proc/cpuinfo`), `nproc` = 4; per CPU L1d 32 KiB and L2 1 MiB, L3 33 MiB shared (as reported by Google Benchmark) |
| Memory | 15 GiB |
| OS | Ubuntu 24.04.5 LTS, Linux 6.18.44, a 4-vCPU VM |
| Compiler | g++ 13.3.0 (`g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0`) |
| Build | CMake 3.28.3, Ninja 1.11.1, `CMAKE_BUILD_TYPE=Release` |
| Flags | `-O3 -DNDEBUG -std=c++20 -fPIC -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion -Wnon-virtual-dtor -Werror` (no `-march=native`: the binary is portable x86-64) |
| Libraries | Google Benchmark 1.8.3 (`libbenchmark-dev 1.8.3-3`), zlib 1.3 (`zlib1g-dev 1:1.3.dfsg-3.1ubuntu2.2`) |

## 4. Commands

From the repository root:

```sh
cmake -S . -B build/gc -G Ninja -DCMAKE_BUILD_TYPE=Release -DSG_BUILD_SERVICES=OFF \
      -DSG_BUILD_BENCHMARKS=ON -DSG_WARNINGS_AS_ERRORS=ON
cmake --build build/gc

# dataset statistics (§2)
./build/gc/libs/graphcore/bench/graphcore_bench --stats

# every benchmark in its own process, 3 repetitions, re-run when other processes used the CPU (§8)
python3 -I libs/graphcore/bench/run_isolated.py ./build/gc/libs/graphcore/bench/graphcore_bench OUT_DIR . 3 8
# (the recommend rows were re-measured the same way after recommend() moved to per-thread scratch)
python3 -I libs/graphcore/bench/run_isolated.py ./build/gc/libs/graphcore/bench/graphcore_bench OUT_DIR '^recommend/' 3 8
```

`OUT_DIR/merged.json` holds every repetition plus mean, median, standard deviation and CV. The
single-process run mentioned in §8 and §10 was the plain Google Benchmark invocation:

```sh
./build/gc/libs/graphcore/bench/graphcore_bench --benchmark_repetitions=3 \
    --benchmark_display_aggregates_only=true --benchmark_out=bench.json --benchmark_out_format=json
```

## 5. All results

Median of 3 repetitions. Exact betweenness is not run where one run would exceed ~30 s (100k: hours;
so2008 with 1 or 2 threads: ≈ 61 s and ≈ 38 s); closeness on 100k is run with 4 threads only.

| Operation | so2008 (real) | 1k | 10k | 100k |
|---|---:|---:|---:|---:|
| CSR build from edge list | 9.24 ms | 62.6 µs | 1.17 ms | 29.3 ms |
| PageRank (to L1 change ≤ 1e-10) | 22.7 ms (45 it.) | 349 µs (56 it.) | 7.61 ms (56 it.) | 112 ms (56 it.) |
| Betweenness exact, 1 thread | — | 90.2 ms | 10.4 s | — |
| Betweenness exact, 2 threads | — | 51.1 ms | 5.78 s | — |
| Betweenness exact, 4 threads | 16.9 s | 27.5 ms | 3.03 s | — |
| Betweenness 256 pivots, 1 thread | 839 ms | — | 264 ms | 4.69 s |
| Betweenness 256 pivots, 2 threads | 517 ms | — | 161 ms | 2.53 s |
| Betweenness 256 pivots, 4 threads | 243 ms | — | 81.1 ms | 1.22 s |
| Closeness, all sources, 1 thread | 771 ms | 1.55 ms | 210 ms | — |
| Closeness, all sources, 4 threads | 187 ms | 623 µs | 58.9 ms | 6.29 s |
| Louvain | 59.5 ms | 1.26 ms | 14.3 ms | 222 ms |
| Recommendations, one user | 267 µs | 11.3 µs | 81.9 µs | 835 µs |
| Shortest path, one query | 951 ns | 1.06 µs | 4.28 µs | 18.7 µs |
| BFS distances, one source | 828 µs | 28.6 µs | 298 µs | 4.39 ms |
| Connected components | 890 µs | 11.6 µs | 289 µs | 5.57 ms |

Queries (shortest path, recommendations, BFS) cycle through the same 4,096 random (from, to) pairs
and 256 random users on every graph. On the real graph shortest paths are cheaper than on the
synthetic graphs of similar size: its hubs keep distances short (mean 3.3 hops against 5.2 on 10k),
so the two search frontiers meet after few levels. Recommendations cost more there (267 µs against
81.9 µs on 10k), which fits the larger two-hop neighbourhoods through hubs (not profiled).

## 6. Speed-up against the legacy implementation

Same graph, same ids and the same query pairs on both sides. Per-source legacy costs come from the
first K users (K given in the benchmark name); "all sources" legacy values are that cost × n and are
extrapolations (§8).

| Graph | Operation | sg::graph | Legacy | Speed-up |
|---|---|---:|---:|---:|
| so2008 | Build graph | 9.24 ms | 24.6 ms | 2.7× |
| so2008 | PageRank, one iteration | 504 µs | 54.3 ms | 108× |
| so2008 | Betweenness per source, 1 / 4 threads | 3.28 ms / 907 µs | 138 ms (K = 32) | 42× / 153× |
| so2008 | Betweenness, all sources | 16.9 s (4 threads) | ≈ 43 min | ≈ 153× |
| so2008 | Closeness per source, 1 / 4 threads | 41.3 µs / 10.1 µs | 67.0 ms (K = 32) | 1,621× / 6,663× |
| so2008 | Closeness, all sources | 771 ms / 187 ms | ≈ 21 min | |
| so2008 | Shortest path, one query | 951 ns | 50.7 ms | 53,290× |
| 1k | Build graph | 62.6 µs | 451 µs | 7.2× |
| 1k | PageRank, one iteration | 6.23 µs | 731 µs | 117× |
| 1k | Betweenness per source, 1 / 4 threads | 90.2 µs / 27.5 µs | 1.57 ms (all 1,000 sources, measured) | 17× / 57× |
| 1k | Closeness per source, 1 / 4 threads | 1.55 µs / 623 ns | 808 µs (all 1,000, measured) | 521× / 1,297× |
| 1k | Shortest path, one query | 1.06 µs | 610 µs | 576× |
| 10k | Build graph | 1.17 ms | 7.52 ms | 6.4× |
| 10k | PageRank, one iteration | 136 µs | 16.9 ms | 124× |
| 10k | Betweenness per source, 1 / 4 threads | 1.04 ms / 303 µs | 30.2 ms (K = 100) | 29× / 100× |
| 10k | Betweenness, all sources | 10.4 s / 3.03 s | ≈ 5.0 min | |
| 10k | Closeness per source, 1 / 4 threads | 21.0 µs / 5.89 µs | 14.1 ms (K = 100) | 670× / 2,389× |
| 10k | Shortest path, one query | 4.28 µs | 10.3 ms | 2,412× |
| 100k | Build graph | 29.3 ms | 251 ms | 8.6× |
| 100k | PageRank, one iteration | 2.00 ms | 696 ms | 347× |
| 100k | Betweenness per source, 1 / 4 threads | 18.3 ms / 4.78 ms (256 pivots) | 915 ms (K = 8) | 50× / 192× |
| 100k | Betweenness, all sources | ≈ 31 min / ≈ 8 min | ≈ 25 h | |
| 100k | Closeness per source, 4 threads | 62.9 µs | 495 ms (K = 8) | 7,876× |
| 100k | Closeness, all sources | 6.29 s (4 threads) | ≈ 14 h | |
| 100k | Shortest path, one query | 18.7 µs | 424 ms | 22,672× |

The PageRank comparison is per iteration because the legacy stopping rule (every node's absolute
change ≤ 1e-4, tightened by 0.9 per round down to 1e-6) is met after 1–4 iterations, when scores
are around 1/n and still far from converged; the library iterates to an L1 change ≤ 1e-10. Full
legacy runs took 217 ms (so2008, 4 iterations), 2.19 ms (1k, 3), 33.8 ms (10k, 2) and 696 ms
(100k, 1).

## 7. Betweenness thread scaling

Same inputs, 1, 2 and 4 worker threads (speed-up relative to 1 thread). The result is bit-identical
for every thread count (sources are cut into fixed blocks whose buffers are merged in block order;
tests `Betweenness.ThreadCountDoesNotChangeASingleBit`,
`Betweenness.SamplingIsDeterministicScaledAndThreadInvariant`).

| Run | 1 thread | 2 threads | 4 threads | 2 / 4 threads speed-up |
|---|---:|---:|---:|---:|
| so2008, 256 pivots | 839 ms | 517 ms | 243 ms | 1.6× / 3.5× |
| so2008, exact (per-source cost vs. the 1-thread pivot run) | 3.28 ms/source | — | 0.907 ms/source (16.9 s) | — / 3.6× |
| 1k, exact | 90.2 ms | 51.1 ms | 27.5 ms | 1.8× / 3.3× |
| 10k, exact | 10.4 s | 5.78 s | 3.03 s | 1.8× / 3.4× |
| 10k, 256 pivots | 264 ms | 161 ms | 81.1 ms | 1.6× / 3.3× |
| 100k, 256 pivots | 4.69 s | 2.53 s | 1.22 s | 1.9× / 3.8× |
| Closeness, so2008 | 771 ms | — | 187 ms | — / 4.1× |
| Closeness, 10k | 210 ms | — | 58.9 ms | — / 3.6× |
| Closeness, 1k | 1.55 ms | — | 623 µs | — / 2.5× |

Betweenness reaches 81–96 % parallel efficiency on 4 threads. Where the remaining time goes was not
profiled; the known overheads of the design are the fork/join per round (blocks are processed in
rounds of 8 blocks per thread so that the ordered merge needs bounded memory) and threads idling at
the end of a round. Closeness on 1k is too small to scale (16 sweeps of 64 sources). The closeness
4.1× on so2008 is above 4 because the 1-thread repetitions varied (713–774 ms, CV 4.5 %); the
single-process run described in §8 measured 721 ms and 194 ms (3.7×). Only 4 vCPUs were
available, so scaling beyond 4 threads was not measured.

## 8. Methodology

* **Harness.** Google Benchmark, wall-clock time (`UseRealTime()`). Every repetition runs at least
  0.5 s or one iteration (library default); tables show the median of 3 repetitions
  (`--benchmark_repetitions=3`). The per-source and per-iteration columns are Google Benchmark
  counters (`kIsIterationInvariantRate | kInvert`), i.e. time per iteration divided by the number of
  sources or PageRank iterations.
* **Shared machine.** The VM was shared with other jobs of this project (C++ and TypeScript builds
  of the services and the frontend). A first single-process run of the whole suite (21:12 UTC)
  overlapped with one of those builds and showed it: exact betweenness on 10k took 6.06 s with 4
  threads, no faster than with 2 (5.96 s). The published numbers therefore come from
  `bench/run_isolated.py`, which runs each benchmark in its own process, measures the CPU time used
  by *other* processes during the run (machine busy time from `/proc/stat` minus the benchmark's
  own `getrusage` time) and repeats the run, up to 8 times and 20 s apart, while that exceeded
  0.15 CPU on average (+0.2 s). The idle background of the VM was ≈ 0.05 CPU. All 72 benchmarks
  ended with an accepted run (the 21:20–21:32 UTC run rejected 9 attempts in total, the recommend
  re-run 1).
  The recommendation rows were re-measured at 21:33 UTC after `recommend()` switched to a
  per-thread scratch area; that change touched only `src/recommend.cpp`.
* **Same inputs.** The legacy baseline is constructed from the same `BenchGraph` (same simple graph,
  same string ids), and query benchmarks of both sides cycle through the same random pairs and users
  (SplitMix64, seed 4242).
* **Extrapolation.** The legacy O(V·E) loops are timed on the first K users (K = all 1,000 on 1k,
  32 on so2008, 100 on 10k, 8 on 100k) and reported per source; "≈" full-graph values are that
  per-source time × n. This is sound because every BFS covers its source's whole connected component
  (99.96 % of the nodes on so2008, 100 % on the synthetic graphs), so the cost per source hardly
  depends on the source. Likewise the library's single-threaded exact betweenness on so2008 and
  100k is the per-source cost of the 256-pivot run × n: the pivot run executes the same kernel,
  only for fewer sources.

## 9. Complexity

V = nodes, E = undirected edges, k = sources or iterations, t = threads, L = BFS levels
(≤ diameter + 1).

| Operation | sg::graph | Legacy | Nature of the gap |
|---|---|---|---|
| Storage | CSR: (V + 1) offsets (8 B) + 2E neighbour ids (4 B) + 2E weights (8 B); so2008: 4.8 MiB | `unordered_map<string, vector<string>>` plus a heap `User` with six strings per node | — |
| Build | O(V + E) for sorted input (counting sort by source; a list is sorted only when it is not already), O(V + E log d_max) worst case | O(V + E) hash inserts and string copies | constant factor |
| PageRank, one iteration | O(V + E) sequential array pulls | O(V + E) hash lookups (two per half-edge), one map insert and string copy per node | constant factor |
| Betweenness, one source | O(V + E): 16-byte node state, predecessors recognised by `dist[v] == dist[w] − 1` (no lists), reset in O(reached) | O(V + E) hash operations, a vector of strings per node for predecessors, an O(V) map rebuilt per source | constant factor |
| Betweenness, all sources | O(V·E) time, O(t·V) memory, parallel, deterministic | O(V·E), single-threaded | + t-way parallelism |
| Closeness, all sources | O(⌈V/64⌉ · L · (V + E)) word operations: 64 sources per sweep | O(V · (V + E)) hash operations | algorithmic |
| Shortest path, one query | bidirectional BFS: O(b^(d/2)) typical, O(V + E) worst | full BFS of the component on every query | algorithmic |
| Communities | Louvain: O(V + E) per local-moving pass, a few passes per level | attribute sorting, not graph-based (audit §4.3) | — |
| Recommendations | O(V log limit + Σ_{z ∈ N(u)} deg z), per-thread scratch | attribute filter (audit §4.4) | — |

The measurements follow these bounds. Per-source betweenness cost tracks E: 90 µs (1k), 1.04 ms
(10k) and 18.3 ms (100k) single-threaded. The 100k step is ~1.8× steeper than E grows, most likely
because the per-node state (100k × 16 B) no longer fits in the 1 MiB L2 (not profiled). Closeness on 100k (6.29 s with 4
threads) shows its L factor: every 64-source sweep runs as many BFS levels as its farthest source
needs (distances up to 9 were seen on that graph).

## 10. Notes and limitations

* **The legacy numbers time wrong answers in places.** Closeness returns +∞ for every node with an
  edge (audit §4.1). Betweenness splits each dependency evenly among a node's predecessors instead
  of in proportion to their shortest-path counts σ. This defect is not listed in `docs/audit.md`;
  it was found while porting and is pinned by the regression test
  `LegacyRegression.BetweennessWeighsPredecessorsByPathCounts`. PageRank stops unconverged (§6).
  The work these routines do is the same as a correct implementation's, so the timings are still
  representative.
* **The legacy duplicate-edge defect (audit §4.2) is not reproduced** in the baseline: both sides
  process the same simple graph, so the comparison isolates data structures and algorithms. With
  the defect the legacy adjacency lists would be twice as long.
* **Run-to-run variation.** Between the two full runs (§8) the library's single-threaded results
  differed by up to 18 % (Louvain on so2008: 70.1 vs 59.5 ms), the legacy code's by up to 1.7×
  (PageRank on so2008: 85.4 vs 54.3 ms per iteration; graph build: 42.8 vs 24.6 ms). Four later
  spot checks of legacy PageRank on so2008, each in a fresh process, gave 66–85 ms per iteration.
  The cause was not isolated: the legacy code is allocation-heavy (strings, hash nodes) and so
  sensitive to heap layout, and other jobs shared the VM. The published legacy PageRank and
  graph-build values are the lowest we observed, so those speed-ups are at the conservative end.
  Read all ratios as accurate to roughly ±20 %. Benchmarks that allocate per iteration vary most
  within a run: the CSR build on so2008 ranged from 5.9 to 10.3 ms across repetitions (CV 27 %).
* **Library build type.** Ubuntu's `libbenchmark` prints `***WARNING*** Library was built as DEBUG.
  Timings may be affected.` That refers to the benchmark library's own out-of-line code. The code
  under test is compiled with `-O3 -DNDEBUG`, and the per-iteration loop of Google Benchmark is
  inline in our translation unit.
* **VM.** CPU frequency could not be pinned, and the vCPU-to-core mapping of the host is unknown.
  Numbers from another machine will differ in absolute terms; the ratios are more portable.
* **Not measured.** Memory use (beyond the CSR layout arithmetic in §9), more than 4 threads, and
  exact betweenness on 100k nodes.

## Appendix: raw medians

Median of 3 repetitions and coefficient of variation across them.

| Benchmark | Median | CV | Counters |
|---|---:|---:|---|
| `dataset_load/so2008` | 171 ms | 18.8 % | |
| `csr_build/so2008` | 9.24 ms | 27.0 % | |
| `pagerank/so2008` | 22.7 ms | 1.7 % | per_iteration 504 µs, iterations 45 |
| `betweenness_exact/so2008/threads:4` | 16.9 s | 1.9 % | per_source 907 µs, sources 18,648 |
| `betweenness_sampled_k256/so2008/threads:1` | 839 ms | 1.6 % | per_source 3.28 ms, sources 256 |
| `betweenness_sampled_k256/so2008/threads:2` | 517 ms | 3.8 % | per_source 2.02 ms |
| `betweenness_sampled_k256/so2008/threads:4` | 243 ms | 1.7 % | per_source 948 µs |
| `closeness/so2008/threads:1` | 771 ms | 4.5 % | per_source 41.3 µs |
| `closeness/so2008/threads:4` | 187 ms | 1.5 % | per_source 10.1 µs |
| `louvain/so2008` | 59.5 ms | 5.8 % | communities 42, modularity 0.3231, levels 3 |
| `recommend/so2008` | 267 µs | 4.5 % | |
| `shortest_path/so2008` | 951 ns | 3.1 % | |
| `bfs_distances/so2008` | 828 µs | 6.2 % | |
| `components/so2008` | 890 µs | 1.1 % | |
| `legacy/build/so2008` | 24.6 ms | 3.8 % | |
| `legacy/pagerank/so2008` | 217 ms | 4.9 % | per_iteration 54.3 ms, iterations 4 |
| `legacy/betweenness/so2008/sources:32` | 4.43 s | 0.4 % | per_source 138 ms |
| `legacy/closeness/so2008/sources:32` | 2.14 s | 10.6 % | per_source 67.0 ms |
| `legacy/shortest_path/so2008` | 50.7 ms | 6.0 % | |
| `csr_build/1k` | 62.6 µs | 2.3 % | |
| `pagerank/1k` | 349 µs | 5.3 % | per_iteration 6.23 µs, iterations 56 |
| `betweenness_exact/1k/threads:1` | 90.2 ms | 4.3 % | per_source 90.2 µs |
| `betweenness_exact/1k/threads:2` | 51.1 ms | 0.9 % | per_source 51.1 µs |
| `betweenness_exact/1k/threads:4` | 27.5 ms | 2.1 % | per_source 27.5 µs |
| `closeness/1k/threads:1` | 1.55 ms | 5.4 % | per_source 1.55 µs |
| `closeness/1k/threads:4` | 623 µs | 5.0 % | per_source 623 ns |
| `louvain/1k` | 1.26 ms | 2.2 % | communities 31, modularity 0.8166, levels 2 |
| `recommend/1k` | 11.3 µs | 2.2 % | |
| `shortest_path/1k` | 1.06 µs | 2.8 % | |
| `bfs_distances/1k` | 28.6 µs | 4.0 % | |
| `components/1k` | 11.6 µs | 2.6 % | |
| `legacy/build/1k` | 451 µs | 3.8 % | |
| `legacy/pagerank/1k` | 2.19 ms | 0.5 % | per_iteration 731 µs, iterations 3 |
| `legacy/betweenness/1k/sources:1000` | 1.57 s | 3.1 % | per_source 1.57 ms |
| `legacy/closeness/1k/sources:1000` | 808 ms | 9.1 % | per_source 808 µs |
| `legacy/shortest_path/1k` | 610 µs | 2.0 % | |
| `csr_build/10k` | 1.17 ms | 10.8 % | |
| `pagerank/10k` | 7.61 ms | 3.9 % | per_iteration 136 µs, iterations 56 |
| `betweenness_exact/10k/threads:1` | 10.4 s | 1.9 % | per_source 1.04 ms |
| `betweenness_exact/10k/threads:2` | 5.78 s | 1.5 % | per_source 578 µs |
| `betweenness_exact/10k/threads:4` | 3.03 s | 0.6 % | per_source 303 µs |
| `betweenness_sampled_k256/10k/threads:1` | 264 ms | 1.5 % | per_source 1.03 ms |
| `betweenness_sampled_k256/10k/threads:2` | 161 ms | 1.2 % | per_source 630 µs |
| `betweenness_sampled_k256/10k/threads:4` | 81.1 ms | 1.5 % | per_source 317 µs |
| `closeness/10k/threads:1` | 210 ms | 3.4 % | per_source 21.0 µs |
| `closeness/10k/threads:4` | 58.9 ms | 5.7 % | per_source 5.89 µs |
| `louvain/10k` | 14.3 ms | 0.7 % | communities 141, modularity 0.8445, levels 3 |
| `recommend/10k` | 81.9 µs | 6.0 % | |
| `shortest_path/10k` | 4.28 µs | 9.2 % | |
| `bfs_distances/10k` | 298 µs | 0.9 % | |
| `components/10k` | 289 µs | 1.0 % | |
| `legacy/build/10k` | 7.52 ms | 7.5 % | |
| `legacy/pagerank/10k` | 33.8 ms | 3.2 % | per_iteration 16.9 ms, iterations 2 |
| `legacy/betweenness/10k/sources:100` | 3.02 s | 4.9 % | per_source 30.2 ms |
| `legacy/closeness/10k/sources:100` | 1.41 s | 19.8 % | per_source 14.1 ms |
| `legacy/shortest_path/10k` | 10.3 ms | 7.8 % | |
| `csr_build/100k` | 29.3 ms | 9.4 % | |
| `pagerank/100k` | 112 ms | 7.1 % | per_iteration 2.00 ms, iterations 56 |
| `betweenness_sampled_k256/100k/threads:1` | 4.69 s | 7.4 % | per_source 18.3 ms |
| `betweenness_sampled_k256/100k/threads:2` | 2.53 s | 5.1 % | per_source 9.90 ms |
| `betweenness_sampled_k256/100k/threads:4` | 1.22 s | 5.9 % | per_source 4.78 ms |
| `closeness/100k/threads:4` | 6.29 s | 1.4 % | per_source 62.9 µs |
| `louvain/100k` | 222 ms | 5.8 % | communities 410, modularity 0.8513, levels 4 |
| `recommend/100k` | 835 µs | 2.9 % | |
| `shortest_path/100k` | 18.7 µs | 1.4 % | |
| `bfs_distances/100k` | 4.39 ms | 2.8 % | |
| `components/100k` | 5.57 ms | 7.1 % | |
| `legacy/build/100k` | 251 ms | 6.5 % | |
| `legacy/pagerank/100k` | 696 ms | 1.0 % | per_iteration 696 ms, iterations 1 |
| `legacy/betweenness/100k/sources:8` | 7.32 s | 5.5 % | per_source 915 ms |
| `legacy/closeness/100k/sources:8` | 3.96 s | 9.1 % | per_source 495 ms |
| `legacy/shortest_path/100k` | 424 ms | 6.7 % | |
