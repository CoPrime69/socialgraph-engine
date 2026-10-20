import { useQuery } from '@tanstack/react-query';
import { graphApi } from '../../api/endpoints';
import { hasStatus } from '../../api/errors';
import { queryKeys } from '../../api/queryKeys';
import { retryDelay, shouldRetry } from '../../app/queryClient';

/**
 * After registration graph-service creates the profile asynchronously (it consumes the
 * `user.registered` event), so `GET /me/profile` answers 404 for a short while. A 404 is
 * therefore polled: every PROFILE_POLL_INTERVAL_MS, up to PROFILE_POLL_ATTEMPTS times.
 */
export const PROFILE_POLL_INTERVAL_MS = 1000;
export const PROFILE_POLL_ATTEMPTS = 20;

export const profilePolling = {
  intervalMs: PROFILE_POLL_INTERVAL_MS,
  attempts: PROFILE_POLL_ATTEMPTS,
};

export function useMyProfile() {
  return useQuery({
    queryKey: queryKeys.myProfile,
    queryFn: ({ signal }) => graphApi.myProfile(signal),
    staleTime: 60_000,
    retry: (failureCount, error) =>
      hasStatus(error, 404)
        ? failureCount < profilePolling.attempts
        : shouldRetry(failureCount, error),
    retryDelay: (attempt, error) =>
      hasStatus(error, 404) ? profilePolling.intervalMs : retryDelay(attempt),
  });
}
