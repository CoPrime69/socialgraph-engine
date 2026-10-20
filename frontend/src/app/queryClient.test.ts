import { describe, expect, it } from 'vitest';
import { ApiError } from '../api/errors';
import {
  createQueryClient,
  provisioningRetry,
  provisioningRetryDelay,
  retryDelay,
  retryWhileProvisioning,
  shouldRetry,
} from './queryClient';

const error = (status: number, code: ApiError['code']) =>
  new ApiError({ status, code, message: '' });

describe('query retry policy', () => {
  it('retries transient failures twice and nothing else', () => {
    expect(shouldRetry(0, error(503, 'unavailable'))).toBe(true);
    expect(shouldRetry(1, error(0, 'network_error'))).toBe(true);
    expect(shouldRetry(2, error(0, 'timeout'))).toBe(false);
    expect(shouldRetry(0, error(500, 'internal'))).toBe(false);
    expect(shouldRetry(0, error(404, 'not_found'))).toBe(false);
    expect(shouldRetry(0, new Error('render bug'))).toBe(false);
  });

  it('backs off exponentially up to 8 seconds', () => {
    expect([0, 1, 2, 3, 4].map(retryDelay)).toEqual([1000, 2000, 4000, 8000, 8000]);
  });

  it('keeps retrying a 404 while a new member is being provisioned', () => {
    provisioningRetry.attempts = 3;
    provisioningRetry.delayMs = 1500;
    const notFound = error(404, 'not_found');
    expect(retryWhileProvisioning(2, notFound)).toBe(true);
    expect(retryWhileProvisioning(3, notFound)).toBe(false);
    expect(retryWhileProvisioning(0, error(500, 'internal'))).toBe(false);
    expect(provisioningRetryDelay(5, notFound)).toBe(1500);
    expect(provisioningRetryDelay(1, error(503, 'unavailable'))).toBe(2000);
  });

  it('configures sensible defaults', () => {
    const client = createQueryClient();
    const defaults = client.getDefaultOptions();
    expect(defaults.queries?.staleTime).toBe(30_000);
    expect(defaults.queries?.refetchOnWindowFocus).toBe(true);
    expect(defaults.mutations?.retry).toBe(false);
  });
});
