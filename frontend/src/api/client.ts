import { ApiError, errorFromResponse } from './errors';

/**
 * Minimal typed fetch client.
 *
 * - Always calls same-origin relative `/api/v1/...` paths (the gateway, or the Vite proxy in dev).
 * - Attaches `Authorization: Bearer <token>` when a token is available.
 * - Every failure rejects with an `ApiError` (HTTP error envelope, non-JSON bodies such as an
 *   nginx 502 page, network failures and timeouts are all normalised).
 * - A 401 on a request that carried a token reports the token as rejected so the session ends.
 */

export type QueryValue = string | number | boolean | null | undefined;
export type QueryParams = Record<string, QueryValue>;

export interface RequestOptions {
  method?: 'GET' | 'POST' | 'PUT' | 'PATCH' | 'DELETE';
  query?: QueryParams;
  body?: unknown;
  signal?: AbortSignal | undefined;
  /** Attach the bearer token (default true). Login/registration/taxonomy pass false. */
  auth?: boolean;
}

export interface ApiResult<T> {
  data: T;
  status: number;
  headers: Headers;
}

export interface ApiClientConfig {
  getToken: () => string | null;
  /** Called when an authenticated request is rejected with 401; `token` is the token that was sent. */
  onUnauthorized: (token: string, error: ApiError) => void;
  timeoutMs: number;
}

const DEFAULT_CONFIG: ApiClientConfig = {
  getToken: () => null,
  onUnauthorized: () => undefined,
  // Matches the gateway's 30 s upstream read timeout. Analytics may legitimately take several
  // seconds right after a change (it waits for `min_version`, then recomputes the metrics).
  timeoutMs: 30_000,
};

let config: ApiClientConfig = { ...DEFAULT_CONFIG };

export function configureApiClient(next: Partial<ApiClientConfig>): void {
  config = { ...config, ...next };
}

export function resetApiClient(): void {
  config = { ...DEFAULT_CONFIG };
}

export function buildUrl(path: string, query?: QueryParams): string {
  const url = new URL(path, window.location.origin);
  if (query) {
    for (const [key, value] of Object.entries(query)) {
      if (value === undefined || value === null || value === '') continue;
      url.searchParams.set(key, String(value));
    }
  }
  return url.toString();
}

function combineSignals(
  timeoutMs: number,
  external?: AbortSignal,
): {
  signal: AbortSignal;
  timedOut: () => boolean;
  dispose: () => void;
} {
  const controller = new AbortController();
  let didTimeout = false;
  const timer = setTimeout(() => {
    didTimeout = true;
    controller.abort();
  }, timeoutMs);
  const onExternalAbort = () => {
    controller.abort();
  };
  if (external) {
    if (external.aborted) controller.abort();
    else external.addEventListener('abort', onExternalAbort, { once: true });
  }
  return {
    signal: controller.signal,
    timedOut: () => didTimeout,
    dispose: () => {
      clearTimeout(timer);
      external?.removeEventListener('abort', onExternalAbort);
    },
  };
}

async function readJson<T>(response: Response): Promise<T> {
  const text = await response.text();
  if (text.trim() === '') return undefined as T;
  try {
    return JSON.parse(text) as T;
  } catch (cause) {
    throw new ApiError({
      status: response.status,
      code: 'invalid_response',
      message: 'The server sent a response that could not be read.',
      requestId: response.headers.get('x-request-id'),
      cause,
    });
  }
}

export async function apiRequest<T>(
  path: string,
  options: RequestOptions = {},
): Promise<ApiResult<T>> {
  const { method = 'GET', query, body, signal, auth = true } = options;
  const headers = new Headers({ Accept: 'application/json' });
  const token = auth ? config.getToken() : null;
  if (token) headers.set('Authorization', `Bearer ${token}`);
  let payload: string | undefined;
  if (body !== undefined) {
    headers.set('Content-Type', 'application/json');
    payload = JSON.stringify(body);
  }

  const abort = combineSignals(config.timeoutMs, signal);
  let response: Response;
  try {
    response = await fetch(buildUrl(path, query), {
      method,
      headers,
      body: payload,
      signal: abort.signal,
      credentials: 'same-origin',
    });
  } catch (cause) {
    abort.dispose();
    if (signal?.aborted) throw cause; // Cancelled by the caller (e.g. TanStack Query): not an error.
    if (abort.timedOut()) {
      throw new ApiError({
        status: 0,
        code: 'timeout',
        message: 'The server took too long to respond.',
        cause,
      });
    }
    throw new ApiError({
      status: 0,
      code: 'network_error',
      message: 'Could not reach the server.',
      cause,
    });
  }

  try {
    if (!response.ok) {
      const error = await errorFromResponse(response);
      if (response.status === 401 && token) config.onUnauthorized(token, error);
      throw error;
    }
    const data = response.status === 204 ? (undefined as T) : await readJson<T>(response);
    return { data, status: response.status, headers: response.headers };
  } finally {
    abort.dispose();
  }
}

export async function apiGet<T>(
  path: string,
  options: Omit<RequestOptions, 'method' | 'body'> = {},
): Promise<T> {
  const result = await apiRequest<T>(path, { ...options, method: 'GET' });
  return result.data;
}
