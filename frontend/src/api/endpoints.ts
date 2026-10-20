import { apiGet, apiRequest } from './client';
import { graphVersionStore } from './graphVersion';
import type {
  AnalyticsSummary,
  AuthResponse,
  CommunitiesResponse,
  ConnectionsResponse,
  CreateConnectionResponse,
  GraphQuery,
  GraphResponse,
  InfluenceMetric,
  InfluencersResponse,
  LoginRequest,
  Paginated,
  PathResponse,
  QuestionDetail,
  QuestionListParams,
  QuestionSummary,
  RecommendationsResponse,
  RegisterRequest,
  RemoveConnectionResponse,
  Tag,
  TagListParams,
  Taxonomy,
  UserActivity,
  UserListParams,
  UserMetrics,
  UserProfileWithCount,
  Versioned,
  WithFreshness,
} from './types';

type Signal = AbortSignal | undefined;

// ---------- identity-service ----------

export const authApi = {
  login: async (body: LoginRequest): Promise<AuthResponse> =>
    (await apiRequest<AuthResponse>('/api/v1/auth/login', { method: 'POST', body, auth: false }))
      .data,

  register: async (body: RegisterRequest): Promise<AuthResponse> =>
    (await apiRequest<AuthResponse>('/api/v1/auth/register', { method: 'POST', body, auth: false }))
      .data,
};

// ---------- graph-service ----------

export const graphApi = {
  taxonomy: (signal?: Signal) => apiGet<Taxonomy>('/api/v1/meta/taxonomy', { auth: false, signal }),

  myProfile: (signal?: Signal) => apiGet<UserProfileWithCount>('/api/v1/me/profile', { signal }),

  users: (params: UserListParams, signal?: Signal) =>
    apiGet<Paginated<UserProfileWithCount>>('/api/v1/users', { query: { ...params }, signal }),

  user: (id: number, signal?: Signal) =>
    apiGet<UserProfileWithCount>(`/api/v1/users/${id}`, { signal }),

  connections: (signal?: Signal) =>
    apiGet<ConnectionsResponse>('/api/v1/me/connections', { signal }),

  connect: async (userId: number): Promise<CreateConnectionResponse> =>
    (
      await apiRequest<CreateConnectionResponse>('/api/v1/me/connections', {
        method: 'POST',
        body: { user_id: userId },
      })
    ).data,

  disconnect: async (userId: number): Promise<RemoveConnectionResponse> =>
    (
      await apiRequest<RemoveConnectionResponse>(`/api/v1/me/connections/${userId}`, {
        method: 'DELETE',
      })
    ).data,
};

// ---------- analytics-service ----------

/**
 * GET against analytics-service with read-your-writes: sends `min_version`, records the
 * returned `graph_version` and exposes `X-Graph-Stale` as `stale`.
 */
async function analyticsGet<T extends Versioned>(
  path: string,
  query: Record<string, string | number | undefined>,
  signal: Signal,
): Promise<WithFreshness<T>> {
  const { data, headers } = await apiRequest<T>(path, {
    query: { ...query, min_version: graphVersionStore.minVersionParam() },
    signal,
  });
  graphVersionStore.observe(data.graph_version);
  return { ...data, stale: headers.get('x-graph-stale')?.toLowerCase() === 'true' };
}

/** Members listed per community card (analytics returns members sorted by PageRank). */
export const COMMUNITY_MEMBER_LIMIT = 24;

export const analyticsApi = {
  summary: (signal?: Signal) =>
    analyticsGet<AnalyticsSummary>('/api/v1/analytics/summary', {}, signal),

  influencers: (metric: InfluenceMetric, limit: number, signal?: Signal) =>
    analyticsGet<InfluencersResponse>('/api/v1/analytics/influencers', { metric, limit }, signal),

  communities: (signal?: Signal) =>
    analyticsGet<CommunitiesResponse>(
      '/api/v1/analytics/communities',
      { member_limit: COMMUNITY_MEMBER_LIMIT },
      signal,
    ),

  graph: (query: GraphQuery, signal?: Signal) =>
    analyticsGet<GraphResponse>(
      '/api/v1/analytics/graph',
      {
        limit: query.limit,
        focus: query.focus,
        depth: query.focus === undefined ? undefined : query.depth,
      },
      signal,
    ),

  recommendations: (limit: number, signal?: Signal) =>
    analyticsGet<RecommendationsResponse>('/api/v1/me/recommendations', { limit }, signal),

  path: (to: number, signal?: Signal) =>
    analyticsGet<PathResponse>('/api/v1/paths', { to }, signal),

  metrics: (userId: number, signal?: Signal) =>
    analyticsGet<UserMetrics>(`/api/v1/users/${userId}/metrics`, {}, signal),
};

// ---------- content-service ----------

export const contentApi = {
  activity: (userId: number, signal?: Signal) =>
    apiGet<UserActivity>(`/api/v1/users/${userId}/activity`, { signal }),

  tags: (params: TagListParams, signal?: Signal) =>
    apiGet<Paginated<Tag>>('/api/v1/tags', { query: { ...params }, signal }),

  questions: (params: QuestionListParams, signal?: Signal) =>
    apiGet<Paginated<QuestionSummary>>('/api/v1/questions', { query: { ...params }, signal }),

  question: (id: number, signal?: Signal) =>
    apiGet<QuestionDetail>(`/api/v1/questions/${id}`, { signal }),
};
