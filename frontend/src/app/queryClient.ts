import { QueryClient } from '@tanstack/react-query';
import { hasStatus, isTransientError } from '../api/errors';

/** Retry transient failures (network, 502/503/504) twice with backoff; never retry 4xx. */
export function shouldRetry(failureCount: number, error: unknown): boolean {
  return failureCount < 2 && isTransientError(error);
}

export function retryDelay(attempt: number): number {
  return Math.min(1000 * 2 ** attempt, 8000);
}

/**
 * analytics-service learns about a new member from an event, a moment after registration. Until
 * then requests that start from the caller (recommendations, paths, the caller's ego graph)
 * answer 404, so those queries retry a 404 a few times before showing it. Mutable for tests.
 */
export const provisioningRetry = {
  attempts: 8,
  delayMs: 1500,
};

export function retryWhileProvisioning(failureCount: number, error: unknown): boolean {
  return hasStatus(error, 404)
    ? failureCount < provisioningRetry.attempts
    : shouldRetry(failureCount, error);
}

export function provisioningRetryDelay(attempt: number, error: unknown): number {
  return hasStatus(error, 404) ? provisioningRetry.delayMs : retryDelay(attempt);
}

export function createQueryClient(): QueryClient {
  return new QueryClient({
    defaultOptions: {
      queries: {
        staleTime: 30_000,
        gcTime: 5 * 60_000,
        retry: shouldRetry,
        retryDelay,
        refetchOnWindowFocus: true,
      },
      mutations: {
        retry: false,
      },
    },
  });
}
