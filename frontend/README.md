# SocialGraph web

The browser front end of SocialGraph: a React single-page app for exploring the 2008 Stack Overflow
community as a social network (influencers, communities, recommendations and degrees of separation)
and for managing your own connections in it. It talks to the four backend services through the
gateway, exactly as specified in [`docs/contracts.md`](../docs/contracts.md) (including the §10
"contract v2" dataset changes).

| Page               | What it shows                                                                                                                                                                      |
| ------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Sign in / Register | Account forms with client validation and server errors mapped to fields                                                                                                            |
| Overview           | Network statistics, the most influential people (PageRank, betweenness, closeness, degree), the largest communities and an interactive network graph                               |
| My network         | Your connections (strength, when and how they were made, remove with confirmation) and recommendations with the reasons behind them                                                |
| Communities        | Every community with its size, dominant technology and most influential members; highlight one in the graph                                                                        |
| People             | Search with filters (category, technology, minimum reputation), sorting and pagination, all kept in the URL                                                                        |
| Person             | Profile with reputation, imported Stack Overflow activity (posts, tags, badges), network metrics and ranks, connect/remove, the chain of people between you, and their ego network |
| Topics / Question  | Questions of the 2008 sample by tag, title search and sort; a question with its answers                                                                                            |

## Quick start

Requires Node.js 22.12 or newer (see `engines` in `package.json`).

```sh
cd frontend
npm ci
cp .env.example .env.local   # optional: only needed if the services are not on the default ports
npm run dev                  # http://localhost:5173
```

The dev server proxies `/api/v1/*` to the individual services (identity 8081, graph 8082,
analytics 8083, content 8084) with the same routing table as the gateway (see
[`config/devProxy.ts`](config/devProxy.ts)). To run against the whole stack instead, start it with
Docker Compose and open the gateway, which serves the production build:

```sh
./scripts/generate-env.sh
docker compose -f deploy/docker-compose.yml --env-file deploy/.env up --build -d
# then open http://localhost:8080
```

## Scripts

| Script                            | Purpose                                                                                      |
| --------------------------------- | -------------------------------------------------------------------------------------------- |
| `npm run dev`                     | Vite dev server with hot reload and the API proxy                                            |
| `npm run build`                   | Type-check (`tsc -b`) and build the production bundle into `dist/`                           |
| `npm run preview`                 | Serve `dist/` locally (port 4173, same API proxy)                                            |
| `npm run typecheck`               | Type-check app, tests, tooling and end-to-end specs                                          |
| `npm run lint`                    | ESLint (typescript-eslint strict + stylistic type-checked, React Hooks), no warnings allowed |
| `npm run format` / `format:check` | Prettier                                                                                     |
| `npm test`                        | Unit and integration tests (Vitest), once                                                    |
| `npm run test:watch`              | Vitest in watch mode                                                                         |
| `npm run test:coverage`           | Tests with V8 coverage and enforced thresholds; HTML report in `coverage/`                   |
| `npm run e2e`                     | Playwright end-to-end tests against a running stack (see below)                              |

## Configuration

| Variable                                                                        | Used by                                                                     | Default                          |
| ------------------------------------------------------------------------------- | --------------------------------------------------------------------------- | -------------------------------- |
| `VITE_IDENTITY_URL`, `VITE_GRAPH_URL`, `VITE_ANALYTICS_URL`, `VITE_CONTENT_URL` | dev/preview proxy targets                                                   | `http://localhost:8081` … `8084` |
| `VITE_DEV_PORT`                                                                 | dev server port                                                             | `5173`                           |
| `E2E_BASE_URL`                                                                  | Playwright base URL                                                         | `http://localhost:8080`          |
| `E2E_DEMO_PASSWORD`                                                             | enables the seeded-demo-member e2e test (`SEED_DEMO_PASSWORD` of the stack) | unset (test skipped)             |
| `E2E_CHROMIUM_PATH`                                                             | use a specific Chromium binary for e2e                                      | Playwright's own                 |

The upstream URLs are read only by `vite.config.ts`; the browser always calls relative
`/api/v1/...` URLs on its own origin, so nothing environment-specific is compiled into the bundle.

## Stack and why

- **React 18 + TypeScript** in strict mode (plus `noUncheckedIndexedAccess`,
  `noImplicitOverride` and `verbatimModuleSyntax`). The API types in
  [`src/api/types.ts`](src/api/types.ts) mirror the contract.
- **Vite 8** for the dev server and build. Every page is a lazily loaded chunk; React, TanStack
  Query and d3 are long-lived vendor chunks, and d3 is only downloaded with the graph.
- **React Router 7** (data router). The app imports `RouterProvider` from the core `react-router`
  entry rather than `react-router/dom`: the `dom` variant only adds `flushSync` navigations, which
  the app does not use, and a single entry point guarantees a single copy of the router contexts
  in every module system (Node's test runner otherwise loads the two entries as separate copies).
- **TanStack Query 5** owns all server state: caching, retries, background refresh, request
  cancellation and placeholder data while paging. There is no global client-state library; the
  only client state shared across the app is the session (a small external store read with
  `useSyncExternalStore`) and the highest graph version seen (same pattern).
- **d3-force, d3-quadtree, d3-zoom** for layout, hit-testing and pan/zoom; drawing is done on a
  `<canvas>` by hand because a few thousand SVG nodes would make the page sluggish.
- **CSS Modules + design tokens** ([`src/styles/tokens.css`](src/styles/tokens.css)): one set of
  custom properties for light and one for dark mode, chosen with `prefers-color-scheme`. The graph
  reads the same tokens at draw time, so it follows the theme too.
- **Vitest, Testing Library, MSW, axe-core** for tests; **Playwright** for end-to-end tests.

## Project layout

```
frontend/
├── config/devProxy.ts        dev/preview proxy: gateway routing table, ordered, unit-tested
├── e2e/                      Playwright specs (run against a real stack)
├── src/
│   ├── api/                  fetch client, ApiError + parsing, endpoints, query hooks, keys, types,
│   │                         graph-version store (read-your-writes)
│   ├── app/                  App, routes, providers, query client policies, client wiring
│   ├── auth/                 session store, AuthProvider (expiry, cross-tab sync), route guards
│   ├── components/           design-system pieces: Button, Card, fields, dialog, toasts, meters,
│   │                         skeletons, empty/error/loading states, pagination, error boundary …
│   ├── features/
│   │   ├── auth/             sign-in and registration
│   │   ├── profile/          ProfileGate: waits for a new member's profile to be provisioned
│   │   ├── overview/         statistics, influencers, largest communities
│   │   ├── network/          connections, recommendations, connect/remove mutations
│   │   ├── communities/      all communities + highlight in graph
│   │   ├── people/           search, filters, pagination
│   │   ├── users/            person page: profile, activity, metrics, path, ego network
│   │   ├── topics/           tags, questions, question detail
│   │   ├── graph/            graph card, canvas renderer, force layout (worker), legend, table
│   │   └── errors/           404 and error pages
│   ├── hooks/                media queries, debounce, countdown, document title
│   ├── layout/               app shell: header, sidebar / bottom navigation
│   ├── lib/                  number/date formatting, text helpers (safe URLs, HTML → text)
│   ├── styles/               tokens and global styles
│   └── test/                 test setup, stateful mock backend, fixtures, DOM shims, axe helper
└── vite.config.ts / vitest.config.ts / playwright.config.ts / eslint.config.js
```

## How it works

### API client and errors

[`src/api/client.ts`](src/api/client.ts) is a small typed wrapper around `fetch`. It adds the
bearer token, applies a 30 s timeout (combined with the caller's `AbortSignal`, so leaving a page
cancels its requests) and turns every failure into an `ApiError` with `status`, `code`,
field `details`, `requestId` (from `X-Request-Id`) and `retryAfterSeconds` (from `Retry-After`):

- a JSON error envelope (`{"error": {"code", "message", "details"}}`) is parsed as is;
- a non-JSON error body, such as the HTML page of a 502 from the gateway, gets a code derived
  from the status;
- network failures and timeouts become status `0` (`network_error` / `timeout`).

`describeError` turns an error into copy for people: server messages of 4xx errors are shown,
5xx details are not (the request id is shown as a reference instead). Queries retry only transient
failures (network, 502/503/504) twice with backoff; mutations never retry automatically.

### Sign-in, registration and the session

- Forms validate on the client with the contract's rules, and a `422` maps the server's field
  `details` onto the same fields (`aria-invalid` + `aria-describedby`); focus moves to the first
  invalid field. A `409` on registration marks the handle as taken. A `429` shows a countdown
  from `Retry-After` and keeps the submit button disabled until it ends. Wrong credentials (`401`)
  show "Incorrect handle or password.", keep the handle and clear the password.
- Registration lets you pick a category and then a technology from the dataset's taxonomy
  (`/meta/taxonomy`); choosing a technology first selects its category.
- Any `401` for the token that is still active ends the session; the sign-in page then explains
  why ("Your session is no longer valid…"). The session also ends at its `expires_at`, and a
  sign-out in one tab signs out the others (`storage` events). Late `401`s for an older token are
  ignored.

#### Token storage

The session (token, expiry and user) is kept in `localStorage` (key `socialgraph.session.v1`) so it
survives reloads and is shared by tabs. The trade-off: any script running on the origin can read
`localStorage`, so a cross-site scripting bug would expose the token. An `HttpOnly` cookie would
avoid that, but the contract has identity-service return the token in the response body and every
service authenticate `Authorization: Bearer`; moving to cookies would need backend changes and CSRF
protection. `sessionStorage` would sign people out in every new tab, and keeping the token only in
memory would sign them out on every reload. The exposure is reduced instead:

- the gateway's Content Security Policy allows only same-origin scripts and connections, and the
  app has no inline scripts and no `dangerouslySetInnerHTML`;
- imported HTML from the dataset (profile "about" texts) is parsed with `DOMParser` and only its
  text is rendered; links to other sites are rendered only for `https:` URLs, with
  `rel="noopener noreferrer"`;
- an expired token is never sent, and the session is cleared on `401` and at expiry.

### New members: profile provisioning

A new account exists in identity-service at once, but graph-service and analytics-service learn
about it from an event a moment later. Right after registration, `GET /me/profile` answers `404`:
the app shows "Setting up your profile…" and polls every second (up to 20 times, then offers
"Try again"). Analytics queries that start from the caller (recommendations, paths, your own ego
graph and metrics) retry a `404` up to 8 times, 1.5 s apart, before showing it.

### Read-your-writes

graph-service returns the new `graph_version` from every connection change, while analytics is an
eventually consistent read model. The app keeps the highest version it has seen
([`src/api/graphVersion.ts`](src/api/graphVersion.ts)) and sends it as `min_version` with every
analytics request; analytics-service waits briefly for its read model or answers with
`X-Graph-Stale: true`. Stale results are shown with an "updating with your latest changes…" note
and refetched every 2.5 s until they are current. After connecting or removing, the lists update
immediately, the buttons involved are disabled while the request runs, a toast confirms the
result, and the affected queries are invalidated so they refetch with the new `min_version`.
A `409` when connecting (already connected) and a `404` when removing (already removed) are
reported as information and refresh the lists.

### The network graph

- **Data.** The overview asks for the top people by PageRank (`?limit=`, 500 to 3,000, default
  1,500). Person pages and My network ask for an ego network (`?focus=<id>&depth=1|2&limit=150`).
  The response's `total_nodes` is used to say what share of the network is drawn.
- **Layout.** d3-force runs in a module Web Worker
  ([`layout.worker.ts`](src/features/graph/layout.worker.ts)) and streams positions back, so the
  page stays responsive; if a worker cannot be started it runs on the main thread. The layout is
  seeded, so the same data always gives the same picture, and positions are reused when the data
  changes. With `prefers-reduced-motion` the layout is computed without animating it.
- **Rendering.** The canvas is redrawn on demand (device-pixel-ratio aware), hit-testing uses a
  quadtree, and d3-zoom handles drag, buttons, Ctrl/⌘ + wheel and pinch. Hovering shows a tooltip;
  clicking opens the person. Dense graphs (more than 4,000 edges) draw a readable backbone of each
  person's strongest connections, with a "Show every connection" switch.
- **Colour.** Communities get eight categorical colours in a fixed order by network-wide size (the
  rest are grey), so a community has the same colour on every page. Circle size encodes PageRank;
  "you" are ringed and labelled. The legend doubles as a highlight filter, and a table view lists
  the same people for anyone who cannot use the picture.

### Loading, empty and error states

Every section has a skeleton while loading, an explanatory empty state and an error state with a
retry button; one failing section does not take the page down. Unknown routes show a 404 page
inside the app shell. Render errors in a page are caught by the route error boundary (the
navigation keeps working), and anything outside the router by a top-level error boundary. A page
chunk that disappeared because a new version was deployed asks for a reload.

## Accessibility

- Landmarks, a skip link, one `h1` per page, page titles, and focus moved to the new page's
  `<main>` after client-side navigation.
- All controls are native elements with visible focus; segmented controls are radio groups, the
  legend and highlight buttons use `aria-pressed`, the confirmation is a native modal `<dialog>`
  that starts on "Cancel" and closes with Escape.
- Meters (connection strength, scores) carry `aria-valuetext`; tables have captions; loading
  states and toasts use live regions (`status` for confirmations, `alert` for errors).
- The graph canvas is an image with a text summary and usage hint, the same people are available
  as a table, and colour is never the only cue.
- Light and dark themes with computed contrast (see the header of `tokens.css`), and
  `prefers-reduced-motion` is respected.
- Every page test runs axe-core (WCAG 2.0/2.1 A and AA rules; colour contrast is not measurable in
  jsdom), and the end-to-end tests run axe in Chromium.

## Security notes

The gateway serves the app with
`Content-Security-Policy: default-src 'self'; img-src 'self' data:; style-src 'self' 'unsafe-inline'; connect-src 'self'; frame-ancestors 'none'`
(see `gateway/security-headers.conf`). The app fits that policy: no inline or third-party scripts,
the layout worker is a same-origin file, avatars are initials rather than remote images, and every
API call is a relative same-origin URL. External links go to `https:` URLs only and open with
`rel="noopener noreferrer"`.

## Testing

```sh
npm test               # all unit and integration tests
npm run test:coverage  # the same with coverage (thresholds: 80 % statements, 75 % branches,
                       # 80 % functions, 80 % lines)
```

- **Page tests** render the real route tree with the real providers
  ([`src/test/render.tsx`](src/test/render.tsx)) against a stateful mock backend built with MSW
  ([`src/test/mockBackend.ts`](src/test/mockBackend.ts)). It follows the contract: bearer tokens,
  error envelopes, profile provisioning, `graph_version` bumps on connection changes,
  `min_version` and `X-Graph-Stale`. It logs every request so tests can assert what the app sent,
  and a request without a mock fails the test. People, connections, tags, questions, answers and
  badges are real rows of the 2008 dataset ([`src/test/fixtures.ts`](src/test/fixtures.ts)); the
  signed-in member and the analytics numbers are made up.
- **Unit tests** cover the API client and error parsing, the session and version stores, query
  policies, formatting and text helpers, validation, the proxy table, and the graph model, layout,
  worker protocol and renderer.
- jsdom lacks media queries, modal dialogs and canvas; small stand-ins live in
  [`src/test/domShims.ts`](src/test/domShims.ts) (the canvas context records its drawing calls).
- Numbers and dates are formatted in the viewer's locale, so the test runner pins `en-US` and UTC.

## End-to-end tests

The Playwright specs in [`e2e/`](e2e) run against a real stack (gateway + services + databases),
one at a time, because they register accounts and change connections:

| Spec              | Covers                                                                                                                                                                                                                      |
| ----------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `journey.spec.ts` | Register, read the real statistics, find the top influencer, see that a new member is not connected yet, connect with a suggestion, see the change in analytics right away, remove it, sign out and in again                |
| `auth.spec.ts`    | Wrong password rejected; protected pages redirect to sign-in; sign-in page has no serious axe violations; the seeded `demo` member sees their 20 connections, topics and an accessible overview (needs `E2E_DEMO_PASSWORD`) |

```sh
./scripts/generate-env.sh                       # once, from the repository root
docker compose -f deploy/docker-compose.yml --env-file deploy/.env up --build -d --wait
cd frontend
npx playwright install chromium                  # skip if browsers are already installed
E2E_BASE_URL=http://localhost:8080 E2E_DEMO_PASSWORD=<SEED_DEMO_PASSWORD> npm run e2e
```

`npx playwright test --list` shows the tests without running them. The gateway rate-limits sign-in
and registration (10 per minute per IP); the suite stays below that.

## Production build

`npm run build` writes static files to `dist/`; the gateway image builds them and serves them with
nginx (hashed assets cached for a year, `index.html` revalidated). Gzipped sizes of the current
build:

| Loaded                                                          | gzip                |
| --------------------------------------------------------------- | ------------------- |
| Every page: entry, React + Router, TanStack Query, API, styles  | ≈ 104.5 kB          |
| Sign in / Register                                              | + 6.2 / 7.8 kB      |
| People / Topics / Question                                      | + 9.9 / 11 / 7.9 kB |
| A page with a graph (Overview, My network, Communities, Person) | + 41.6-46.8 kB¹     |
| Layout worker, started with the first graph                     | ≈ 6.5 kB            |

¹ About 31 kB of it (the graph card and d3) is shared by these pages and downloaded once.

## Contract notes

Interpretations made where the contract leaves room:

- Community shares on the Communities page are each community's size over the sum of all sizes,
  since `/analytics/communities` returns every community and everyone belongs to exactly one.
  The overview uses `summary.users` as the denominator.
- Imported connections (`origin: "imported"`) are described by their interactions ("73
  interactions since Sep 2008"); connections made in the app by the date they were made.
- Members who joined directly have no Stack Overflow link (only `https:` `external_url`s are
  linked) and no imported activity; their page says so instead of showing empty lists.
- Degrees of separation are shown as the chain of people from `GET /paths`; `found: false` and a
  `404` for a member not yet known to analytics have their own messages.
