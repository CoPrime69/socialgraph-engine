# ADR-0009: React + TypeScript + Vite, TanStack Query, canvas graph

## Context
The original "UI" was a console menu plus a Graphviz PNG. The new UI must cover sign-in, overview,
connections and recommendations, communities and people search, with robust loading/error states.

## Decision
React 18 with strict TypeScript, built by Vite; React Router for pages; TanStack Query for all server
state (caching, retries, invalidation after mutations, `min_version` refetches); a typed fetch client
that turns the error envelope into a typed `ApiError`; d3-force simulation rendered on `<canvas>` for
the network graph; Vitest + Testing Library + MSW for component tests; Playwright for end-to-end tests.

## Alternatives
* **Redux / Zustand for server data** – duplicates what a server-state cache does.
* **Cytoscape.js / Sigma.js** – capable graph libraries; d3-force + canvas keeps the bundle smaller and
  gives full control over styling and interaction for a few thousand nodes.

## Consequences
* UI state is mostly derived from queries; the only client state is the auth token and view filters.
* See `frontend/README.md` for structure and the token-storage trade-off.
