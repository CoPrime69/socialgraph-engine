import { keepPreviousData, useQuery } from '@tanstack/react-query';
import { provisioningRetryDelay, retryWhileProvisioning } from '../app/queryClient';
import { analyticsApi, contentApi, graphApi } from './endpoints';
import { queryKeys } from './queryKeys';
import type {
  GraphQuery,
  InfluenceMetric,
  QuestionListParams,
  TagListParams,
  UserListParams,
} from './types';

/**
 * Query hooks shared by the pages. Analytics data that came back with `X-Graph-Stale: true`
 * (the read model had not caught up with this tab's last change yet) is refetched every
 * STALE_REFETCH_MS until it is fresh.
 */
export const STALE_REFETCH_MS = 2500;

function refetchWhileStale(query: { state: { data?: { stale: boolean } | undefined } }) {
  return query.state.data?.stale ? STALE_REFETCH_MS : false;
}

// ---------- graph-service ----------

export function useTaxonomy() {
  return useQuery({
    queryKey: queryKeys.taxonomy,
    queryFn: ({ signal }) => graphApi.taxonomy(signal),
    staleTime: Number.POSITIVE_INFINITY,
  });
}

export function useMyConnections() {
  return useQuery({
    queryKey: queryKeys.myConnections,
    queryFn: ({ signal }) => graphApi.connections(signal),
  });
}

export function useUserList(params: UserListParams) {
  return useQuery({
    queryKey: queryKeys.userList(params),
    queryFn: ({ signal }) => graphApi.users(params, signal),
    placeholderData: keepPreviousData,
  });
}

export function useUser(id: number) {
  return useQuery({
    queryKey: queryKeys.user(id),
    queryFn: ({ signal }) => graphApi.user(id, signal),
  });
}

// ---------- analytics-service ----------

export function useSummary() {
  return useQuery({
    queryKey: queryKeys.summary,
    queryFn: ({ signal }) => analyticsApi.summary(signal),
    refetchInterval: refetchWhileStale,
  });
}

export function useInfluencers(metric: InfluenceMetric, limit: number) {
  return useQuery({
    queryKey: queryKeys.influencers(metric, limit),
    queryFn: ({ signal }) => analyticsApi.influencers(metric, limit, signal),
    placeholderData: keepPreviousData,
    refetchInterval: refetchWhileStale,
  });
}

export function useCommunities() {
  return useQuery({
    queryKey: queryKeys.communities,
    queryFn: ({ signal }) => analyticsApi.communities(signal),
    staleTime: 60_000,
    refetchInterval: refetchWhileStale,
  });
}

/**
 * `startsFromMe`: the scope is the caller's own ego network, which answers 404 until the
 * read model knows a newly registered member.
 */
export function useGraph(query: GraphQuery, startsFromMe = false) {
  return useQuery({
    queryKey: queryKeys.graph(query),
    queryFn: ({ signal }) => analyticsApi.graph(query, signal),
    placeholderData: keepPreviousData,
    staleTime: 60_000,
    refetchInterval: refetchWhileStale,
    ...(startsFromMe ? { retry: retryWhileProvisioning, retryDelay: provisioningRetryDelay } : {}),
  });
}

export function useRecommendations(limit: number) {
  return useQuery({
    queryKey: queryKeys.recommendations(limit),
    queryFn: ({ signal }) => analyticsApi.recommendations(limit, signal),
    refetchInterval: refetchWhileStale,
    retry: retryWhileProvisioning,
    retryDelay: provisioningRetryDelay,
  });
}

export function usePath(to: number, enabled: boolean) {
  return useQuery({
    queryKey: queryKeys.path(to),
    queryFn: ({ signal }) => analyticsApi.path(to, signal),
    enabled,
    refetchInterval: refetchWhileStale,
    retry: retryWhileProvisioning,
    retryDelay: provisioningRetryDelay,
  });
}

export function useUserMetrics(id: number) {
  return useQuery({
    queryKey: queryKeys.metrics(id),
    queryFn: ({ signal }) => analyticsApi.metrics(id, signal),
    refetchInterval: refetchWhileStale,
    retry: retryWhileProvisioning,
    retryDelay: provisioningRetryDelay,
  });
}

// ---------- content-service (read-only dataset) ----------

const CONTENT_STALE_MS = 10 * 60_000;

export function useUserActivity(id: number) {
  return useQuery({
    queryKey: queryKeys.activity(id),
    queryFn: ({ signal }) => contentApi.activity(id, signal),
    staleTime: CONTENT_STALE_MS,
  });
}

export function useTags(params: TagListParams) {
  return useQuery({
    queryKey: queryKeys.tags(params),
    queryFn: ({ signal }) => contentApi.tags(params, signal),
    staleTime: CONTENT_STALE_MS,
    placeholderData: keepPreviousData,
  });
}

export function useQuestions(params: QuestionListParams) {
  return useQuery({
    queryKey: queryKeys.questions(params),
    queryFn: ({ signal }) => contentApi.questions(params, signal),
    staleTime: CONTENT_STALE_MS,
    placeholderData: keepPreviousData,
  });
}

export function useQuestion(id: number) {
  return useQuery({
    queryKey: queryKeys.question(id),
    queryFn: ({ signal }) => contentApi.question(id, signal),
    staleTime: CONTENT_STALE_MS,
  });
}
