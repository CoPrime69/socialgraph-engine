import { delay, http, HttpResponse } from 'msw';
import { describe, expect, it, vi } from 'vitest';
import { server } from '../test/server';
import { apiGet, apiRequest, buildUrl, configureApiClient } from './client';
import { ApiError } from './errors';

const URL_PATTERN = '*/api/v1/echo';

async function rejection(promise: Promise<unknown>): Promise<ApiError> {
  try {
    await promise;
  } catch (error) {
    if (error instanceof ApiError) return error;
    throw error;
  }
  throw new Error('Expected the request to fail');
}

describe('buildUrl', () => {
  it('builds same-origin URLs and skips empty query values', () => {
    const url = new URL(
      buildUrl('/api/v1/users', {
        q: 'jon',
        category: '',
        branch: undefined,
        min: null,
        limit: 20,
        ok: false,
      }),
    );
    expect(url.origin).toBe(window.location.origin);
    expect(url.pathname).toBe('/api/v1/users');
    expect([...url.searchParams]).toEqual([
      ['q', 'jon'],
      ['limit', '20'],
      ['ok', 'false'],
    ]);
  });
});

describe('apiRequest', () => {
  it('sends JSON with the bearer token and returns data, status and headers', async () => {
    configureApiClient({ getToken: () => 'secret-token' });
    let seen: { auth: string | null; type: string | null; body: unknown } | null = null;
    server.use(
      http.post(URL_PATTERN, async ({ request }) => {
        seen = {
          auth: request.headers.get('authorization'),
          type: request.headers.get('content-type'),
          body: await request.json(),
        };
        return HttpResponse.json({ ok: true }, { status: 201, headers: { 'X-Thing': 'yes' } });
      }),
    );
    const result = await apiRequest<{ ok: boolean }>('/api/v1/echo', {
      method: 'POST',
      body: { user_id: 42 },
    });
    expect(result.data).toEqual({ ok: true });
    expect(result.status).toBe(201);
    expect(result.headers.get('x-thing')).toBe('yes');
    expect(seen).toEqual({
      auth: 'Bearer secret-token',
      type: 'application/json',
      body: { user_id: 42 },
    });
  });

  it('does not send the token when auth is false', async () => {
    configureApiClient({ getToken: () => 'secret-token' });
    let auth: string | null = 'unset';
    server.use(
      http.get(URL_PATTERN, ({ request }) => {
        auth = request.headers.get('authorization');
        return HttpResponse.json({});
      }),
    );
    await apiGet('/api/v1/echo', { auth: false });
    expect(auth).toBeNull();
  });

  it('returns undefined for 204 and empty bodies', async () => {
    server.use(http.delete(URL_PATTERN, () => new HttpResponse(null, { status: 204 })));
    const result = await apiRequest('/api/v1/echo', { method: 'DELETE' });
    expect(result.data).toBeUndefined();
  });

  it('parses the error envelope, including field details', async () => {
    server.use(
      http.post(URL_PATTERN, () =>
        HttpResponse.json(
          {
            error: {
              code: 'validation_failed',
              message: 'Invalid input',
              details: [{ field: 'handle', message: 'must be 3-32 characters' }],
            },
          },
          { status: 422, headers: { 'X-Request-Id': 'req-7' } },
        ),
      ),
    );
    const error = await rejection(apiRequest('/api/v1/echo', { method: 'POST', body: {} }));
    expect(error).toMatchObject({
      status: 422,
      code: 'validation_failed',
      message: 'Invalid input',
      requestId: 'req-7',
      fromEnvelope: true,
    });
    expect(error.details).toEqual([{ field: 'handle', message: 'must be 3-32 characters' }]);
  });

  it('turns a non-JSON 502 from the gateway into an ApiError', async () => {
    server.use(
      http.get(
        URL_PATTERN,
        () =>
          new HttpResponse('<html><body><h1>502 Bad Gateway</h1></body></html>', {
            status: 502,
            headers: { 'Content-Type': 'text/html' },
          }),
      ),
    );
    const error = await rejection(apiGet('/api/v1/echo'));
    expect(error).toMatchObject({ status: 502, code: 'unavailable', fromEnvelope: false });
  });

  it('reports a network failure as network_error', async () => {
    server.use(http.get(URL_PATTERN, () => HttpResponse.error()));
    const error = await rejection(apiGet('/api/v1/echo'));
    expect(error).toMatchObject({ status: 0, code: 'network_error' });
    expect(error.message).toBe('Could not reach the server.');
  });

  it('reports an unreadable success body as invalid_response', async () => {
    server.use(http.get(URL_PATTERN, () => new HttpResponse('{"broken":', { status: 200 })));
    const error = await rejection(apiGet('/api/v1/echo'));
    expect(error).toMatchObject({ status: 200, code: 'invalid_response' });
  });

  it('times out slow responses', async () => {
    configureApiClient({ timeoutMs: 30 });
    server.use(
      http.get(URL_PATTERN, async () => {
        await delay(500);
        return HttpResponse.json({});
      }),
    );
    const error = await rejection(apiGet('/api/v1/echo'));
    expect(error).toMatchObject({ status: 0, code: 'timeout' });
  });

  it('rethrows cancellation by the caller as is', async () => {
    server.use(
      http.get(URL_PATTERN, async () => {
        await delay(500);
        return HttpResponse.json({});
      }),
    );
    const controller = new AbortController();
    const request = apiGet('/api/v1/echo', { signal: controller.signal });
    controller.abort();
    await expect(request).rejects.not.toBeInstanceOf(ApiError);
  });

  it('honours a signal that is already aborted', async () => {
    server.use(http.get(URL_PATTERN, () => HttpResponse.json({})));
    const controller = new AbortController();
    controller.abort();
    await expect(apiGet('/api/v1/echo', { signal: controller.signal })).rejects.not.toBeInstanceOf(
      ApiError,
    );
  });

  it('reports a rejected token once, with the token that was sent', async () => {
    const onUnauthorized = vi.fn();
    configureApiClient({ getToken: () => 'old-token', onUnauthorized });
    server.use(
      http.get(URL_PATTERN, () =>
        HttpResponse.json(
          { error: { code: 'unauthenticated', message: 'expired' } },
          { status: 401 },
        ),
      ),
    );
    const error = await rejection(apiGet('/api/v1/echo'));
    expect(error.code).toBe('unauthenticated');
    expect(onUnauthorized).toHaveBeenCalledTimes(1);
    expect(onUnauthorized).toHaveBeenCalledWith('old-token', error);
  });

  it('does not report a 401 for requests sent without a token (failed sign-in)', async () => {
    const onUnauthorized = vi.fn();
    configureApiClient({ getToken: () => 'token', onUnauthorized });
    server.use(
      http.post(URL_PATTERN, () =>
        HttpResponse.json(
          { error: { code: 'invalid_credentials', message: 'Invalid handle or password' } },
          { status: 401 },
        ),
      ),
    );
    const error = await rejection(
      apiRequest('/api/v1/echo', { method: 'POST', body: {}, auth: false }),
    );
    expect(error.code).toBe('invalid_credentials');
    expect(onUnauthorized).not.toHaveBeenCalled();
  });
});
