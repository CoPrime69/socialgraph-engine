import type {
  GraphQuery,
  InfluenceMetric,
  QuestionListParams,
  TagListParams,
  UserListParams,
} from './types';

/**
 * Query keys are grouped by the service that owns the data so that invalidation after a
 * graph mutation can target exactly the affected caches:
 *   ['me', …]        graph-service, caller-specific
 *   ['users', …]     graph-service profiles (connection_count changes after mutations)
 *   ['analytics', …] analytics read model (refetched with min_version)
 *   ['content', …]   content-service (read-only dataset, never invalidated by mutations)
 */
export const queryKeys = {
  taxonomy: ['meta', 'taxonomy'] as const,

  me: ['me'] as const,
  myProfile: ['me', 'profile'] as const,
  myConnections: ['me', 'connections'] as const,

  users: ['users'] as const,
  userList: (params: UserListParams) => ['users', 'list', params] as const,
  user: (id: number) => ['users', 'detail', id] as const,

  analytics: ['analytics'] as const,
  summary: ['analytics', 'summary'] as const,
  influencers: (metric: InfluenceMetric, limit: number) =>
    ['analytics', 'influencers', metric, limit] as const,
  communities: ['analytics', 'communities'] as const,
  graph: (query: GraphQuery) => ['analytics', 'graph', query] as const,
  recommendations: (limit: number) => ['analytics', 'recommendations', limit] as const,
  path: (to: number) => ['analytics', 'path', to] as const,
  metrics: (id: number) => ['analytics', 'metrics', id] as const,

  activity: (id: number) => ['content', 'activity', id] as const,
  tags: (params: TagListParams) => ['content', 'tags', params] as const,
  questions: (params: QuestionListParams) => ['content', 'questions', params] as const,
  question: (id: number) => ['content', 'question', id] as const,
};
