/**
 * Dev/preview proxy that mirrors the gateway routing table in docs/contracts.md §2 and §10.6.
 *
 * In production the nginx gateway serves the SPA and routes `/api/v1/*` on the same origin,
 * so the browser only ever calls relative `/api/v1/...` URLs. During `vite` / `vite preview`
 * this table forwards those calls to the individual services instead.
 *
 * Every route is an anchored regular expression (Vite treats proxy keys that start with `^`
 * as RegExp) and Vite uses the FIRST matching key in insertion order, so the specific
 * `/api/v1/users/{id}/metrics` (analytics) and `/api/v1/users/{id}/activity` (content) routes
 * are listed before the generic graph-service `/api/v1/users` prefix.
 */

export interface Upstreams {
  identity: string;
  graph: string;
  analytics: string;
  content: string;
}

export type ServiceName = keyof Upstreams;

export const DEFAULT_UPSTREAMS: Readonly<Upstreams> = {
  identity: 'http://localhost:8081',
  graph: 'http://localhost:8082',
  analytics: 'http://localhost:8083',
  content: 'http://localhost:8084',
};

/** Environment variable that overrides each upstream. */
export const UPSTREAM_ENV: Readonly<Record<ServiceName, string>> = {
  identity: 'VITE_IDENTITY_URL',
  graph: 'VITE_GRAPH_URL',
  analytics: 'VITE_ANALYTICS_URL',
  content: 'VITE_CONTENT_URL',
};

/** Matches `path` exactly, or followed by `/`, `?` or `#` (so `/api/v1/paths?to=1` matches). */
function prefix(path: string): string {
  const escaped = path.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
  return `^${escaped}(?:[/?#]|$)`;
}

/** `/api/v1/users/{id}/<suffix>` with nothing after the suffix but a query string. */
function userSubresource(suffix: string): string {
  return `^/api/v1/users/[^/?#]+/${suffix}(?:[?#]|$)`;
}

/** Ordered routing table: first match wins (same semantics as Vite's proxy middleware). */
export const API_ROUTES: readonly { pattern: string; service: ServiceName }[] = [
  { pattern: prefix('/api/v1/auth'), service: 'identity' },
  // analytics-service — before the generic /api/v1/users route.
  { pattern: userSubresource('metrics'), service: 'analytics' },
  { pattern: prefix('/api/v1/analytics'), service: 'analytics' },
  { pattern: prefix('/api/v1/paths'), service: 'analytics' },
  { pattern: prefix('/api/v1/me/recommendations'), service: 'analytics' },
  // content-service (§10.6) — also before the generic /api/v1/users route.
  { pattern: userSubresource('activity'), service: 'content' },
  { pattern: prefix('/api/v1/tags'), service: 'content' },
  { pattern: prefix('/api/v1/questions'), service: 'content' },
  // graph-service
  { pattern: prefix('/api/v1/users'), service: 'graph' },
  { pattern: prefix('/api/v1/me/profile'), service: 'graph' },
  { pattern: prefix('/api/v1/me/connections'), service: 'graph' },
  { pattern: prefix('/api/v1/meta'), service: 'graph' },
];

export interface ProxyRule {
  target: string;
  changeOrigin: boolean;
  xfwd: boolean;
}

/** Reads `VITE_IDENTITY_URL`, `VITE_GRAPH_URL`, `VITE_ANALYTICS_URL` and `VITE_CONTENT_URL`. */
export function resolveUpstreams(env: Record<string, string | undefined>): Upstreams {
  const pick = (service: ServiceName): string => {
    const value = env[UPSTREAM_ENV[service]]?.trim();
    return value ? value.replace(/\/+$/, '') : DEFAULT_UPSTREAMS[service];
  };
  return {
    identity: pick('identity'),
    graph: pick('graph'),
    analytics: pick('analytics'),
    content: pick('content'),
  };
}

/** Builds the `server.proxy` / `preview.proxy` object. Key order is significant. */
export function createDevProxy(upstreams: Upstreams): Record<string, ProxyRule> {
  const proxy: Record<string, ProxyRule> = {};
  for (const route of API_ROUTES) {
    proxy[route.pattern] = { target: upstreams[route.service], changeOrigin: true, xfwd: true };
  }
  return proxy;
}

/**
 * Resolves which upstream a request URL (path + query) is sent to, using exactly the
 * algorithm of Vite's proxy middleware (iterate keys in order, `^`-keys are RegExp,
 * other keys are string prefixes). Returns `undefined` when no rule matches.
 */
export function resolveProxyTarget(
  proxy: Record<string, ProxyRule>,
  url: string,
): string | undefined {
  for (const [context, rule] of Object.entries(proxy)) {
    const matches = context.startsWith('^')
      ? new RegExp(context).test(url)
      : url.startsWith(context);
    if (matches) return rule.target;
  }
  return undefined;
}
