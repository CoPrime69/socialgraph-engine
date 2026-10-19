// The traffic mix: what a population of signed-in members does, as weighted operations across every
// service. Weights are percentages of all requests. Reads dominate, as on any social site; about 6%
// of requests change the graph, which also keeps the analytics read model busy re-ranking.
//
// Each operation builds one request for a given virtual member and lists the statuses that are a
// correct answer (e.g. 409 when connecting to someone you already follow); anything else, any 5xx,
// timeout or socket error, counts as an error.

const pick = (array) => array[Math.floor(Math.random() * array.length)];
const enc = encodeURIComponent;
const METRICS = ['pagerank', 'betweenness', 'closeness', 'degree'];

export const OPERATIONS = [
  // graph-service: profiles and connections
  { name: 'search members', weight: 9, ok: [200], build: (u, s) => ({ path: `/api/v1/users?q=${enc(s.nameQuery())}&limit=20` }) },
  { name: 'view profile', weight: 14, ok: [200], build: (u, s) => ({ path: `/api/v1/users/${s.userId()}` }) },
  { name: 'my connections', weight: 7, ok: [200], build: () => ({ path: '/api/v1/me/connections' }) },
  { name: 'connect', weight: 4, ok: [201, 409], write: true, build: (u, s) => ({ method: 'POST', path: '/api/v1/me/connections', body: { user_id: s.userId() } }) },
  {
    name: 'disconnect', weight: 2, ok: [200, 404], write: true,
    build: (u) => {
      const id = u.connections.size ? pick([...u.connections]) : null;
      return id === null ? null : { method: 'DELETE', path: `/api/v1/me/connections/${id}`, target: id };
    },
  },
  // analytics-service: rankings, communities, recommendations, paths, per-member metrics
  { name: 'influencers', weight: 8, ok: [200], build: () => ({ path: `/api/v1/analytics/influencers?metric=${pick(METRICS)}&limit=20` }) },
  { name: 'recommendations', weight: 8, ok: [200], build: () => ({ path: '/api/v1/me/recommendations?limit=10' }) },
  { name: 'degrees of separation', weight: 5, ok: [200], build: (u, s) => ({ path: `/api/v1/paths?to=${s.userId()}` }) },
  { name: 'member metrics', weight: 5, ok: [200], build: (u, s) => ({ path: `/api/v1/users/${s.userId()}/metrics` }) },
  { name: 'network summary', weight: 3, ok: [200], build: () => ({ path: '/api/v1/analytics/summary' }) },
  { name: 'communities', weight: 2, ok: [200], build: () => ({ path: '/api/v1/analytics/communities?member_limit=10' }) },
  { name: 'ego network graph', weight: 3, ok: [200], build: (u, s) => ({ path: `/api/v1/analytics/graph?focus=${s.userId()}&depth=1&limit=200` }) },
  // content-service: Stack Overflow activity, questions and tags
  { name: 'member activity', weight: 9, ok: [200], build: (u, s) => ({ path: `/api/v1/users/${s.userId()}/activity` }) },
  { name: 'questions by tag', weight: 8, ok: [200], build: (u, s) => ({ path: `/api/v1/questions?tag=${enc(s.tag())}&sort=${pick(['score', 'recent'])}&limit=20` }) },
  { name: 'search questions', weight: 5, ok: [200], build: (u, s) => ({ path: `/api/v1/questions?q=${enc(s.searchText())}&limit=20` }) },
  { name: 'view question', weight: 6, ok: [200], build: (u, s) => ({ path: `/api/v1/questions/${s.questionId()}` }) },
  { name: 'browse tags', weight: 2, ok: [200], build: (u, s) => ({ path: `/api/v1/tags?q=${enc(s.tagPrefix())}&limit=20` }) },
];

const total = OPERATIONS.reduce((sum, op) => sum + op.weight, 0);
if (total !== 100) throw new Error(`operation weights must sum to 100, got ${total}`);

const cumulative = [];
OPERATIONS.reduce((sum, op) => { cumulative.push(sum + op.weight); return sum + op.weight; }, 0);

export function pickOperation() {
  const r = Math.random() * 100;
  return OPERATIONS[cumulative.findIndex((c) => r < c)];
}
