# Phase 1: Audit of the original system

This is the audit performed before any code was changed. The original sources are kept,
unmodified, under [`legacy/`](../legacy) so every finding below can be checked against them.

## 1. Original problem statement

From the original README and the code:

> A C++ application that analyzes a social network represented as a graph. Nodes are users,
> edges are relationships. The application identifies key **influencers**, detects
> **communities**, and **recommends new connections**, giving insight into the structure of
> the network and helping users manage their connections.

Concretely, the original program offered:

| # | Capability | Where |
|---|------------|-------|
| F1 | Register / log in a user (name, id, password, category, influence, branch) | `login_manager.cpp`, `main.cpp` |
| F2 | Rank influencers with PageRank (top 5 printed) | `ConnectionManager::calculatePageRank` |
| F3 | Betweenness centrality (Brandes) and closeness centrality | `calculateBetweennessCentrality`, `calculateClosenessCentrality` |
| F4 | Detect communities | `detectCommunities` |
| F5 | Recommend connections using category / branch / influence similarity | `recommendConnectionsForNewUser`, `print_utilities.cpp` |
| F6 | View, add and remove one's own connections | `connection_handler.cpp` |
| F7 | Weighted "connection strength" between users | `adjacency_matrix.cpp` |
| F8 | Render the network, coloured by community, to a PNG (Graphviz) | `graph_visualizer.cpp` |
| F9 | BFS shortest path helper (`bfsPath`, implemented but never exposed) | `connection_manager.cpp` |
| F10 | Persist users and connection weights to CSV | `user_data.csv`, `adjacency_matrix.csv` |

Dataset: 100 seed users across 5 categories (Engineering, Finance, HR, Marketing, Sales) and
6 branches, each with an `influence` score in 0-100, plus two users added by manual testing.

## 2. Stack and structure

* ~2,400 lines of C++ (no stated standard), one interactive console executable.
* Build: `build.bat` only (Windows `cmd`), compiles with `g++` and immediately runs the program.
  No CMake, no Linux/macOS build, no `-fopenmp` even though OpenMP pragmas are used.
* Directories named `cpp files/` and `header files/` (spaces), `#include <bits/stdc++.h>`
  (GCC-only) and `using namespace std;` in every header.
* No tests, no CI, no container, no API. README lists an `adjacency_matrix.csv` that was deleted.
* 7.9 MB of generated PNGs committed to the repository.

## 3. Behaviour observed when running it

The legacy program was compiled with `g++ -O2` on Linux and driven through stdin:

* **Results change on every run.** Logging in as `user1` twice in a row printed different
  "Top 5 influencers" (`User 95, User 98, …` vs `User 97, User 19, …`). Connections are
  regenerated from `std::random_device` at start-up and communities are randomised.
* **Two contradictory sources of truth for "connections".** Centrality and communities use an
  in-memory adjacency list that is randomly regenerated each run and never saved; "View your
  connections" uses `adjacency_matrix.csv` (random weights ≥ 13). The influencer ranking and the
  user's visible connections therefore describe different graphs.
* **The recommendation filter is a no-op.** `printRecommendedConnections` keeps a pair when
  *either* side matches the user's category/branch, but the first side *is* the user, so every
  pair passes. `user1` (Engineering / software) is recommended Sales and Finance users.
* Graph visualisation needs the external `dot` binary and fails without it.

## 4. Defects found by reading the code

Correctness

1. **Closeness centrality is always `inf`.** In `GraphTraversal::bfsDistance` the edge callback
   reads `distances[to]` via `operator[]`, default-inserting `0.0`, so `new_dist < distances[to]`
   is never true; every distance is 0 and `(reachable-1)/0` is infinite.
2. **Duplicate edges.** `establishConnections` visits each unordered pair twice ((i,j) and (j,i))
   and appends to both adjacency lists each time, producing a multigraph that double-counts paths
   in PageRank and betweenness.
3. **Community detection is not graph-based.** Users are sorted by branch/category and cut into
   chunks of random size (`MAX±4`), then nudged by a 3-pass heuristic. It is non-deterministic,
   has no quality measure, and is recomputed on every add/remove.
4. **Recommendation scoring has dead and degenerate terms.** Candidates from the same community
   are excluded and then given a +0.3 "same community" bonus that can never apply; the user's
   *name* is part of the Jaccard set; influence similarity is `1 - min(|Δ|, 1)` on a 0-100 scale,
   i.e. zero unless influences differ by < 1; seed users are only recommended to people whose
   matrix weight is exactly 0, which almost never happens.
5. **Inconsistent thresholds.** A connection "exists" at weight ≥ 13; the message says ≥ 15; the
   visualiser draws edges ≥ 15.
6. **Undefined behaviour.** `getRandomPosition` and `getLuminance` have no `return`.
7. **Data races (latent).** Under OpenMP, betweenness calls `connections[curr]` (an inserting
   `operator[]`) and PageRank writes `new_pagerank[id]` into a shared `unordered_map` from
   several threads. It only "works" because `build.bat` forgot `-fopenmp`.
8. `LoginManager::saveUsers` can never write anything (it skips users that exist, i.e. all).

Security

9. Passwords stored and committed in plain text (`user_data.csv`).
10. CSV written without escaping: a name containing a comma corrupts the file. `cin >> name`
    cannot read names with spaces.
11. Free-text category/branch produced inconsistent data (`Electrical` vs `electrical`,
    `HR` used as a branch).

Resource management and performance

12. Raw `new User` never freed; `AdjacencyMatrix` never deleted (destructor commented out).
13. Dense `n×n` matrix of doubles, rewritten in full to CSV on every single mutation (O(n²) I/O).
14. String-keyed hash maps in every inner loop; `getUser` and `findCommunityIndex` are linear
    scans, making `updateConnection` O(n) and community lookups O(n) per call.

## 5. What is preserved, refactored, or redesigned

| Area | Decision | Reason |
|------|----------|--------|
| Domain (users with category, branch, influence; undirected connections) | **Preserve** | It is the problem statement. |
| Seed dataset (100 users) | **Preserve** (passwords removed) | Same demo network; plaintext passwords cannot be kept. |
| Influencers via PageRank, betweenness, closeness | **Preserve, re-implement correctly** | Fix closeness, duplicates, races; add normalisation and tests against brute force. |
| Recommendations by category/branch/influence | **Preserve intent, redesign scoring** | Combine the original attribute affinity with friends-of-friends structure (Adamic-Adar); explain every score. |
| Connection strength (F7) | **Preserve, make deterministic** | Derived from attribute affinity instead of random noise. |
| Community detection | **Redesign** | Replace random chunking with Louvain modularity optimisation (deterministic, measurable). |
| Shortest path (F9) | **Expose** | Already implemented but unreachable; becomes "degrees of separation". |
| Visualisation (F8) | **Redesign** | Interactive browser graph; DOT export kept for Graphviz users. |
| CSV persistence | **Replace** | PostgreSQL with constraints, migrations and transactions (see ADR-0003). |
| Console UI | **Replace** | HTTP API + React frontend. |
| `build.bat` | **Replace** | CMake + presets, Docker, CI. |
