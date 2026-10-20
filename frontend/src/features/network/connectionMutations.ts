import { useMutation, useQueryClient, type QueryClient } from '@tanstack/react-query';
import { graphApi } from '../../api/endpoints';
import { describeError, hasStatus } from '../../api/errors';
import { graphVersionStore } from '../../api/graphVersion';
import { queryKeys } from '../../api/queryKeys';
import type {
  Connection,
  ConnectionsResponse,
  RecommendationsResponse,
  WithFreshness,
} from '../../api/types';
import { useToast } from '../../components/toastContext';

/** The person a connection is made with or removed from. */
export interface ConnectionTarget {
  id: number;
  name: string;
}

const RECOMMENDATIONS_KEY = [...queryKeys.analytics, 'recommendations'] as const;

/**
 * After a connection changes, refetch everything derived from the graph. Analytics requests
 * carry `min_version` (the version returned by the mutation, recorded in graphVersionStore), so
 * the refetched analytics already include the change (read-your-writes, contracts §6).
 * Content-service data does not depend on connections and is left alone.
 */
export async function invalidateAfterGraphChange(queryClient: QueryClient): Promise<void> {
  await Promise.all([
    queryClient.invalidateQueries({ queryKey: queryKeys.analytics }),
    queryClient.invalidateQueries({ queryKey: queryKeys.me }),
    queryClient.invalidateQueries({ queryKey: queryKeys.users }),
  ]);
}

function byStrength(a: Connection, b: Connection): number {
  return b.strength - a.strength || a.user.id - b.user.id;
}

export function useConnect() {
  const queryClient = useQueryClient();
  const toast = useToast();
  return useMutation({
    mutationFn: (target: ConnectionTarget) => graphApi.connect(target.id),
    onSuccess: async (result, target) => {
      const { graph_version: graphVersion, ...connection } = result;
      graphVersionStore.observe(graphVersion);
      // Show the change immediately; the invalidation below then brings the server's view.
      await queryClient.cancelQueries({ queryKey: RECOMMENDATIONS_KEY });
      queryClient.setQueryData<ConnectionsResponse>(queryKeys.myConnections, (old) =>
        old
          ? {
              graph_version: graphVersion,
              items: [...old.items.filter((item) => item.user.id !== target.id), connection].sort(
                byStrength,
              ),
            }
          : old,
      );
      queryClient.setQueriesData<WithFreshness<RecommendationsResponse>>(
        { queryKey: RECOMMENDATIONS_KEY },
        (old) =>
          old ? { ...old, items: old.items.filter((item) => item.user.id !== target.id) } : old,
      );
      toast.success(`Connected with ${target.name}`);
      void invalidateAfterGraphChange(queryClient);
    },
    onError: (error, target) => {
      if (hasStatus(error, 401)) return; // the session ends and the sign-in page explains why
      if (hasStatus(error, 409)) {
        toast.info(`You're already connected with ${target.name}`);
        void invalidateAfterGraphChange(queryClient);
        return;
      }
      if (hasStatus(error, 404)) {
        toast.error(`Couldn't connect with ${target.name}`, 'This person could not be found.');
        return;
      }
      toast.error(`Couldn't connect with ${target.name}`, describeError(error).message);
    },
  });
}

export function useDisconnect() {
  const queryClient = useQueryClient();
  const toast = useToast();
  return useMutation({
    mutationFn: (target: ConnectionTarget) => graphApi.disconnect(target.id),
    onSuccess: (result, target) => {
      graphVersionStore.observe(result.graph_version);
      queryClient.setQueryData<ConnectionsResponse>(queryKeys.myConnections, (old) =>
        old
          ? {
              graph_version: result.graph_version,
              items: old.items.filter((item) => item.user.id !== target.id),
            }
          : old,
      );
      toast.success(`Removed your connection with ${target.name}`);
      void invalidateAfterGraphChange(queryClient);
    },
    onError: (error, target) => {
      if (hasStatus(error, 401)) return;
      if (hasStatus(error, 404)) {
        toast.info(`You're no longer connected with ${target.name}`);
        void invalidateAfterGraphChange(queryClient);
        return;
      }
      toast.error(`Couldn't remove ${target.name}`, describeError(error).message);
    },
  });
}
