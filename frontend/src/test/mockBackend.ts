/**
 * Stateful in-memory stand-in for the backend services (identity, graph, analytics and content),
 * implemented as MSW request handlers that follow docs/contracts.md. It starts from the fixtures,
 * applies connection changes (bumping `graph_version`), honours `min_version`/`X-Graph-Stale`,
 * requires a bearer token where the real services do, and logs every request so tests can
 * assert what the app sent.
 */
import { http, HttpResponse, type PathParams } from 'msw';
import type {
  Connection,
  FieldErrorDetail,
  InfluenceMetric,
  LoginRequest,
  QuestionDetail,
  RegisterRequest,
  UserProfileWithCount,
} from '../api/types';
import { normalizeTags } from '../lib/text';
import * as fixtures from './fixtures';

/** Password of the fixture member `ada_member` (see fixtures.me). */
export const PASSWORD = 'correct-horse-battery';
export const TOKEN_EXPIRY = '2099-01-01T00:00:00Z';

export interface LoggedRequest {
  method: string;
  path: string;
  query: URLSearchParams;
  body: unknown;
  token: string | null;
}

interface Account {
  id: number;
  handle: string;
  name: string;
  password: string;
}

interface PendingProfile {
  profile: UserProfileWithCount;
  /** GET /me/profile calls still answered with 404 (provisioning in progress). */
  pollsLeft: number;
}

interface BackendState {
  graphVersion: number;
  /** Version applied by the analytics read model; lags behind while `analyticsPaused`. */
  analyticsVersion: number;
  analyticsPaused: boolean;
  users: Map<number, UserProfileWithCount>;
  accounts: Map<string, Account>;
  tokens: Map<string, number>;
  connections: Map<number, Connection[]>;
  pendingProfiles: Map<number, PendingProfile>;
  /** How many profile polls a newly registered member waits before the profile exists. */
  provisioningPolls: number;
  /** Targets for which GET /paths finds no path. */
  unreachable: Set<number>;
  nextId: number;
  log: LoggedRequest[];
  unhandled: string[];
}

function initialState(): BackendState {
  const users = new Map<number, UserProfileWithCount>();
  for (const profile of [fixtures.me, ...fixtures.people]) users.set(profile.id, { ...profile });
  return {
    graphVersion: fixtures.summary.graph_version,
    analyticsVersion: fixtures.summary.graph_version,
    analyticsPaused: false,
    users,
    accounts: new Map([
      [
        fixtures.me.handle,
        {
          id: fixtures.me.id,
          handle: fixtures.me.handle,
          name: fixtures.me.name,
          password: PASSWORD,
        },
      ],
    ]),
    tokens: new Map([[fixtures.authResponse.token, fixtures.me.id]]),
    connections: new Map([
      [fixtures.me.id, fixtures.myConnections.map((connection) => ({ ...connection }))],
      [fixtures.jonSkeet.id, [{ ...fixtures.importedConnection }]],
    ]),
    pendingProfiles: new Map(),
    provisioningPolls: 2,
    unreachable: new Set(),
    nextId: fixtures.me.id + 1,
    log: [],
    unhandled: [],
  };
}

let state = initialState();

export const backend = {
  get state(): BackendState {
    return state;
  },

  reset(): void {
    state = initialState();
  },

  /** Requests to `path` (exact pathname), optionally only those with `method`. */
  requests(path: string, method?: string): LoggedRequest[] {
    return state.log.filter(
      (entry) => entry.path === path && (method === undefined || entry.method === method),
    );
  },

  lastRequest(path: string, method?: string): LoggedRequest | undefined {
    return backend.requests(path, method).at(-1);
  },

  connectionsOf(userId: number): Connection[] {
    return state.connections.get(userId) ?? [];
  },

  /** Lets analytics catch up with every graph change made while it was paused. */
  resumeAnalytics(): void {
    state.analyticsPaused = false;
    state.analyticsVersion = state.graphVersion;
  },
};

// ---------- helpers ----------

export function errorResponse(
  status: number,
  code: string,
  message: string,
  options: { details?: FieldErrorDetail[]; headers?: Record<string, string> } = {},
): Response {
  return HttpResponse.json(
    { error: { code, message, ...(options.details ? { details: options.details } : {}) } },
    { status, headers: { 'X-Request-Id': 'req-test-1', ...options.headers } },
  );
}

interface RouteContext {
  request: Request;
  params: PathParams;
  url: URL;
  body: unknown;
  /** Caller's user id (routes with `auth: false` get null). */
  userId: number;
}

type Resolver = (context: RouteContext) => Response | undefined | Promise<Response | undefined>;

/**
 * Declares a handler that logs the request and enforces the bearer token. Tests use it for
 * overrides too, so that their requests show up in the log.
 */
export function route(
  method: 'get' | 'post' | 'delete',
  path: string,
  resolver: Resolver,
  { auth = true }: { auth?: boolean } = {},
) {
  return http[method](path, async ({ request, params }) => {
    const url = new URL(request.url);
    let body: unknown = undefined;
    if (method === 'post') {
      try {
        body = await request.clone().json();
      } catch {
        body = undefined;
      }
    }
    const token = request.headers.get('authorization')?.replace(/^Bearer\s+/i, '') ?? null;
    state.log.push({
      method: request.method,
      path: url.pathname,
      query: url.searchParams,
      body,
      token,
    });
    const userId = token === null ? undefined : state.tokens.get(token);
    if (auth && userId === undefined) {
      return errorResponse(401, 'unauthenticated', 'Missing or invalid access token.');
    }
    return resolver({ request, params, url, body, userId: userId ?? 0 });
  });
}

function intParam(url: URL, name: string, fallback: number, max = Number.MAX_SAFE_INTEGER): number {
  const raw = url.searchParams.get(name);
  if (raw === null || !/^\d+$/.test(raw)) return fallback;
  return Math.min(max, Number(raw));
}

function paginate<T>(items: T[], url: URL, defaultLimit: number, maxLimit: number) {
  const limit = Math.max(1, intParam(url, 'limit', defaultLimit, maxLimit));
  const offset = intParam(url, 'offset', 0);
  return { items: items.slice(offset, offset + limit), total: items.length, limit, offset };
}

/** Analytics response: current read-model version, `X-Graph-Stale` when behind `min_version`. */
function analyticsJson(url: URL, body: object): Response {
  const minVersion = intParam(url, 'min_version', 0);
  const stale = minVersion > state.analyticsVersion;
  return HttpResponse.json(
    { ...body, graph_version: state.analyticsVersion },
    { headers: stale ? { 'X-Graph-Stale': 'true' } : {} },
  );
}

function bumpVersion(): number {
  state.graphVersion += 1;
  if (!state.analyticsPaused) state.analyticsVersion = state.graphVersion;
  return state.graphVersion;
}

function changeCount(userId: number, delta: number): void {
  const profile = state.users.get(userId);
  if (profile) profile.connection_count = Math.max(0, profile.connection_count + delta);
}

function idParam(params: PathParams, name: string): number | null {
  const raw = params[name];
  return typeof raw === 'string' && /^\d+$/.test(raw) ? Number(raw) : null;
}

function notFound(what: string): Response {
  return errorResponse(404, 'not_found', `${what} not found.`);
}

function memberProfile(id: number, request: RegisterRequest): UserProfileWithCount {
  const now = new Date().toISOString();
  return {
    id,
    handle: request.handle,
    name: request.name,
    category: request.category,
    branch: request.branch,
    influence: 0,
    reputation: 1,
    location: request.location ?? null,
    website_url: null,
    about: request.about ?? null,
    views: 0,
    up_votes: 0,
    down_votes: 0,
    source: 'member',
    external_url: null,
    created_at: now,
    last_access_at: now,
    connection_count: 0,
  };
}

const METRICS = new Set<string>(['pagerank', 'betweenness', 'closeness', 'degree']);

// ---------- handlers ----------

export const handlers = [
  // identity-service
  route(
    'post',
    '*/api/v1/auth/login',
    ({ body }) => {
      const { handle, password } = body as LoginRequest;
      const account = state.accounts.get(handle);
      if (!account) return errorResponse(401, 'invalid_credentials', 'Invalid handle or password.');
      if (account.password !== password) {
        return errorResponse(401, 'invalid_credentials', 'Invalid handle or password.');
      }
      const token =
        account.id === fixtures.me.id ? fixtures.authResponse.token : `token-${account.handle}`;
      state.tokens.set(token, account.id);
      return HttpResponse.json({
        token,
        expires_at: TOKEN_EXPIRY,
        user: { id: account.id, handle: account.handle, name: account.name },
      });
    },
    { auth: false },
  ),

  route(
    'post',
    '*/api/v1/auth/register',
    ({ body }) => {
      const request = body as RegisterRequest;
      const taken =
        state.accounts.has(request.handle) ||
        [...state.users.values()].some((user) => user.handle === request.handle);
      if (taken) return errorResponse(409, 'conflict', 'Handle is already taken.');
      const id = state.nextId++;
      const token = `token-${request.handle}`;
      state.accounts.set(request.handle, {
        id,
        handle: request.handle,
        name: request.name,
        password: request.password,
      });
      state.tokens.set(token, id);
      state.pendingProfiles.set(id, {
        profile: memberProfile(id, request),
        pollsLeft: state.provisioningPolls,
      });
      return HttpResponse.json(
        {
          token,
          expires_at: TOKEN_EXPIRY,
          user: { id, handle: request.handle, name: request.name },
        },
        { status: 201 },
      );
    },
    { auth: false },
  ),

  // graph-service
  route('get', '*/api/v1/meta/taxonomy', () => HttpResponse.json(fixtures.taxonomy), {
    auth: false,
  }),

  route('get', '*/api/v1/me/profile', ({ userId }) => {
    const pending = state.pendingProfiles.get(userId);
    if (pending) {
      if (pending.pollsLeft > 0) {
        pending.pollsLeft -= 1;
        return notFound('Profile');
      }
      state.pendingProfiles.delete(userId);
      state.users.set(userId, pending.profile);
      state.connections.set(userId, []);
    }
    const profile = state.users.get(userId);
    return profile ? HttpResponse.json(profile) : notFound('Profile');
  }),

  route('get', '*/api/v1/me/connections', ({ userId }) =>
    HttpResponse.json({ items: backend.connectionsOf(userId), graph_version: state.graphVersion }),
  ),

  route('post', '*/api/v1/me/connections', ({ body, userId }) => {
    const targetId = (body as { user_id?: unknown } | undefined)?.user_id;
    if (typeof targetId !== 'number') {
      return errorResponse(400, 'bad_request', 'user_id must be a number.');
    }
    if (targetId === userId) {
      return errorResponse(422, 'validation_failed', 'You cannot connect with yourself.', {
        details: [{ field: 'user_id', message: 'must not be the caller' }],
      });
    }
    const target = state.users.get(targetId);
    if (!target) return notFound('User');
    const mine = backend.connectionsOf(userId);
    if (mine.some((connection) => connection.user.id === targetId)) {
      return errorResponse(409, 'conflict', 'Already connected.');
    }
    const { connection_count: _count, ...user } = target;
    const connection: Connection = {
      user,
      strength: 0.64,
      interactions: 0,
      origin: 'member',
      connected_at: new Date().toISOString(),
      first_interaction_at: null,
      last_interaction_at: null,
    };
    state.connections.set(
      userId,
      [...mine, connection].sort((a, b) => b.strength - a.strength),
    );
    changeCount(userId, 1);
    changeCount(targetId, 1);
    const graphVersion = bumpVersion();
    return HttpResponse.json({ ...connection, graph_version: graphVersion }, { status: 201 });
  }),

  route('delete', '*/api/v1/me/connections/:userId', ({ params, userId }) => {
    const targetId = idParam(params, 'userId');
    const mine = backend.connectionsOf(userId);
    if (targetId === null || !mine.some((connection) => connection.user.id === targetId)) {
      return notFound('Connection');
    }
    state.connections.set(
      userId,
      mine.filter((connection) => connection.user.id !== targetId),
    );
    changeCount(userId, -1);
    changeCount(targetId, -1);
    return HttpResponse.json({ graph_version: bumpVersion() });
  }),

  // analytics-service (before the generic /users/:id route)
  route('get', '*/api/v1/users/:userId/metrics', ({ params, url }) => {
    const id = idParam(params, 'userId');
    const profile = id === null ? undefined : state.users.get(id);
    if (!profile) return notFound('User');
    return analyticsJson(url, fixtures.metricsFor(profile));
  }),

  // content-service (before the generic /users/:id route)
  route('get', '*/api/v1/users/:userId/activity', ({ params }) => {
    const id = idParam(params, 'userId');
    if (id === null || !state.users.has(id)) return notFound('User');
    return HttpResponse.json(
      id === fixtures.jonSkeet.id ? fixtures.jonSkeetActivity : fixtures.emptyActivity(id),
    );
  }),

  route('get', '*/api/v1/users/:userId', ({ params }) => {
    const id = idParam(params, 'userId');
    const profile = id === null ? undefined : state.users.get(id);
    return profile ? HttpResponse.json(profile) : notFound('User');
  }),

  route('get', '*/api/v1/users', ({ url }) => {
    const q = (url.searchParams.get('q') ?? '').trim().toLowerCase();
    const category = url.searchParams.get('category');
    const branch = url.searchParams.get('branch');
    const minReputation = intParam(url, 'min_reputation', 0);
    const sort = url.searchParams.get('sort') ?? 'reputation';
    const items = [...state.users.values()]
      .filter(
        (user) =>
          (q === '' || user.name.toLowerCase().includes(q) || user.handle.includes(q)) &&
          (category === null || user.category === category) &&
          (branch === null || user.branch === branch) &&
          user.reputation >= minReputation,
      )
      .sort((a, b) => {
        if (sort === 'name') return a.name.localeCompare(b.name) || a.id - b.id;
        if (sort === 'id') return a.id - b.id;
        return b.reputation - a.reputation || a.id - b.id;
      });
    return HttpResponse.json(paginate(items, url, 20, 100));
  }),

  // analytics-service
  route('get', '*/api/v1/analytics/summary', ({ url }) => analyticsJson(url, fixtures.summary)),

  route('get', '*/api/v1/analytics/influencers', ({ url }) => {
    const metric = url.searchParams.get('metric') ?? 'pagerank';
    if (!METRICS.has(metric)) {
      return errorResponse(
        400,
        'bad_request',
        'metric must be pagerank, betweenness, closeness or degree.',
      );
    }
    const response = fixtures.influencers(metric as InfluenceMetric);
    const limit = intParam(url, 'limit', 10, 100);
    return analyticsJson(url, { ...response, items: response.items.slice(0, limit) });
  }),

  route('get', '*/api/v1/analytics/communities', ({ url }) =>
    analyticsJson(url, fixtures.communities),
  ),

  route('get', '*/api/v1/analytics/graph', ({ url }) => {
    const limit = intParam(url, 'limit', 1500, 100_000);
    const focus = url.searchParams.get('focus');
    if (focus === null) return analyticsJson(url, fixtures.topGraph(limit));
    const profile = state.users.get(Number(focus));
    if (!profile) return notFound('User');
    return analyticsJson(url, fixtures.egoGraph(profile, backend.connectionsOf(profile.id)));
  }),

  route('get', '*/api/v1/me/recommendations', ({ url, userId }) => {
    if (!state.users.has(userId)) return notFound('User');
    const connected = new Set(
      backend.connectionsOf(userId).map((connection) => connection.user.id),
    );
    const items = fixtures.recommendations.filter(
      (recommendation) =>
        recommendation.user.id !== userId && !connected.has(recommendation.user.id),
    );
    return analyticsJson(url, { items: items.slice(0, intParam(url, 'limit', 10, 50)) });
  }),

  route('get', '*/api/v1/paths', ({ url, userId }) => {
    const target = state.users.get(intParam(url, 'to', 0));
    const self = state.users.get(userId);
    if (!target || !self) return notFound('User');
    if (state.unreachable.has(target.id)) {
      return analyticsJson(url, { found: false, degrees: null, path: [] });
    }
    if (backend.connectionsOf(userId).some((connection) => connection.user.id === target.id)) {
      return analyticsJson(url, {
        found: true,
        degrees: 1,
        path: [fixtures.userRef(self), fixtures.userRef(target)],
      });
    }
    const path = fixtures.pathTo(target);
    return analyticsJson(url, { ...path, path: [fixtures.userRef(self), ...path.path.slice(1)] });
  }),

  // content-service
  route('get', '*/api/v1/tags', ({ url }) => {
    const q = (url.searchParams.get('q') ?? '').trim().toLowerCase();
    const items = fixtures.tags.filter((tag) => q === '' || tag.name.startsWith(q));
    return HttpResponse.json(paginate(items, url, 20, 100));
  }),

  route('get', '*/api/v1/questions/:questionId', ({ params }) => {
    const id = idParam(params, 'questionId');
    if (id === fixtures.questionDetail.id) return HttpResponse.json(fixtures.questionDetail);
    const summary = fixtures.questions.find((question) => question.id === id);
    if (!summary) return notFound('Question');
    const detail: QuestionDetail = { ...summary, answers: [], comment_count: 0 };
    return HttpResponse.json(detail);
  }),

  route('get', '*/api/v1/questions', ({ url }) => {
    const tag = url.searchParams.get('tag');
    const q = (url.searchParams.get('q') ?? '').trim().toLowerCase();
    const sort = url.searchParams.get('sort') ?? 'score';
    const items = fixtures.questions
      .filter(
        (question) =>
          (tag === null || normalizeTags(question.tags).includes(tag)) &&
          (q === '' || question.title.toLowerCase().includes(q)),
      )
      .sort((a, b) =>
        sort === 'recent' ? b.created_at.localeCompare(a.created_at) : b.score - a.score,
      );
    return HttpResponse.json(paginate(items, url, 20, 100));
  }),

  // Anything else under /api/v1 is a missing mock: record it so the test fails loudly.
  http.all('*/api/v1/*', ({ request }) => {
    const url = new URL(request.url);
    state.unhandled.push(`${request.method} ${url.pathname}${url.search}`);
    return errorResponse(501, 'internal', 'No mock handler for this request.');
  }),
];
