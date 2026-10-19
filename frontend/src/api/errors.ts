import type { FieldErrorDetail } from './types';

/** Error codes defined by the contract (§3), including `invalid_credentials` for failed logins. */
export type ServerErrorCode =
  | 'bad_request'
  | 'unauthenticated'
  | 'invalid_credentials'
  | 'not_found'
  | 'conflict'
  | 'payload_too_large'
  | 'validation_failed'
  | 'rate_limited'
  | 'unavailable'
  | 'internal';

/** Codes synthesised by the client when there is no usable error envelope. */
export type ClientErrorCode = 'network_error' | 'timeout' | 'invalid_response' | 'http_error';

export type ApiErrorCode = ServerErrorCode | ClientErrorCode;

export interface ApiErrorInit {
  status: number;
  code: ApiErrorCode;
  message: string;
  details?: readonly FieldErrorDetail[];
  requestId?: string | null;
  retryAfterSeconds?: number | null;
  /** True when the server answered with the standard `{ "error": { … } }` envelope. */
  fromEnvelope?: boolean;
  cause?: unknown;
}

/** Every failed API call rejects with an ApiError (never a bare TypeError/SyntaxError). */
export class ApiError extends Error {
  /** HTTP status, or 0 when no response was received (network failure / timeout). */
  readonly status: number;
  readonly code: ApiErrorCode;
  readonly details: readonly FieldErrorDetail[];
  readonly requestId: string | null;
  readonly retryAfterSeconds: number | null;
  readonly fromEnvelope: boolean;

  constructor(init: ApiErrorInit) {
    super(init.message, init.cause === undefined ? undefined : { cause: init.cause });
    this.name = 'ApiError';
    this.status = init.status;
    this.code = init.code;
    this.details = init.details ?? [];
    this.requestId = init.requestId ?? null;
    this.retryAfterSeconds = init.retryAfterSeconds ?? null;
    this.fromEnvelope = init.fromEnvelope ?? false;
  }
}

export function isApiError(error: unknown): error is ApiError {
  return error instanceof ApiError;
}

export function hasStatus(error: unknown, status: number): boolean {
  return isApiError(error) && error.status === status;
}

const SERVER_CODES = new Set<string>([
  'bad_request',
  'unauthenticated',
  'invalid_credentials',
  'not_found',
  'conflict',
  'payload_too_large',
  'validation_failed',
  'rate_limited',
  'unavailable',
  'internal',
]);

/** Maps an HTTP status to a contract code when the body is not a usable envelope. */
export function codeForStatus(status: number): ApiErrorCode {
  switch (status) {
    case 400:
      return 'bad_request';
    case 401:
      return 'unauthenticated';
    case 404:
      return 'not_found';
    case 409:
      return 'conflict';
    case 413:
      return 'payload_too_large';
    case 422:
      return 'validation_failed';
    case 429:
      return 'rate_limited';
    case 502:
    case 503:
    case 504:
      return 'unavailable';
    default:
      return status >= 500 ? 'internal' : 'http_error';
  }
}

/** Parses `Retry-After` as delta-seconds or an HTTP-date (RFC 9110 §10.2.3). */
export function parseRetryAfter(value: string | null, now: number = Date.now()): number | null {
  if (value === null) return null;
  const trimmed = value.trim();
  if (trimmed === '') return null;
  if (/^\d+$/.test(trimmed)) return Number(trimmed);
  const date = Date.parse(trimmed);
  if (Number.isNaN(date)) return null;
  return Math.max(0, Math.ceil((date - now) / 1000));
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}

function isFieldDetail(value: unknown): value is FieldErrorDetail {
  return isRecord(value) && typeof value.field === 'string' && typeof value.message === 'string';
}

interface ParsedEnvelope {
  code: string;
  message: string;
  details: FieldErrorDetail[];
}

/** Returns the envelope when `text` is `{ "error": { "code": string, … } }`, else null. */
export function parseErrorEnvelope(text: string): ParsedEnvelope | null {
  if (text.trim() === '') return null;
  let parsed: unknown;
  try {
    parsed = JSON.parse(text);
  } catch {
    return null;
  }
  if (!isRecord(parsed) || !isRecord(parsed.error)) return null;
  const { code, message, details } = parsed.error;
  if (typeof code !== 'string' || code === '') return null;
  return {
    code,
    message: typeof message === 'string' ? message : '',
    details: Array.isArray(details) ? details.filter(isFieldDetail) : [],
  };
}

const FALLBACK_MESSAGES: Record<ApiErrorCode, string> = {
  bad_request: 'The request was not accepted.',
  unauthenticated: 'Your session is no longer valid.',
  invalid_credentials: 'Incorrect handle or password.',
  not_found: 'The requested item was not found.',
  conflict: 'That conflicts with the current state.',
  payload_too_large: 'The request is too large.',
  validation_failed: 'Some values are not valid.',
  rate_limited: 'Too many attempts. Please wait and try again.',
  unavailable: 'The service is temporarily unavailable.',
  internal: 'Something went wrong on our side.',
  network_error: 'Could not reach the server.',
  timeout: 'The server took too long to respond.',
  invalid_response: 'The server sent a response that could not be read.',
  http_error: 'The request failed.',
};

/** Converts any non-2xx Response into an ApiError, tolerating HTML/empty bodies (e.g. a 502 from nginx). */
export async function errorFromResponse(response: Response): Promise<ApiError> {
  const requestId = response.headers.get('x-request-id');
  const retryAfterSeconds = parseRetryAfter(response.headers.get('retry-after'));
  let text = '';
  try {
    text = await response.text();
  } catch {
    // Body could not be read; fall back to the status code.
  }
  const envelope = parseErrorEnvelope(text);
  if (envelope) {
    const code = SERVER_CODES.has(envelope.code)
      ? (envelope.code as ServerErrorCode)
      : codeForStatus(response.status);
    return new ApiError({
      status: response.status,
      code,
      message: envelope.message || FALLBACK_MESSAGES[code],
      details: envelope.details,
      requestId,
      retryAfterSeconds,
      fromEnvelope: true,
    });
  }
  const code = codeForStatus(response.status);
  return new ApiError({
    status: response.status,
    code,
    message: `${FALLBACK_MESSAGES[code]} (HTTP ${response.status})`,
    requestId,
    retryAfterSeconds,
    fromEnvelope: false,
  });
}

export interface ErrorDescription {
  title: string;
  message: string;
}

function formatWait(seconds: number): string {
  if (seconds < 60) return `${seconds} second${seconds === 1 ? '' : 's'}`;
  const minutes = Math.ceil(seconds / 60);
  return `${minutes} minute${minutes === 1 ? '' : 's'}`;
}

/** User-facing copy for an error, safe to render (never leaks stack traces). */
export function describeError(error: unknown): ErrorDescription {
  if (!isApiError(error)) {
    return {
      title: 'Something went wrong',
      message: 'An unexpected error occurred. Please try again.',
    };
  }
  switch (error.code) {
    case 'network_error':
      return {
        title: "Can't reach the server",
        message: 'Check your connection and try again.',
      };
    case 'timeout':
      return {
        title: 'The server is taking too long',
        message: 'Please try again in a moment.',
      };
    case 'unavailable':
      return {
        title: 'Service temporarily unavailable',
        message: 'Part of the platform is starting up or busy. Please try again in a moment.',
      };
    case 'internal':
      return {
        title: 'Something went wrong on our side',
        message: 'The error has been logged. Please try again.',
      };
    case 'rate_limited':
      return {
        title: 'Too many attempts',
        message:
          error.retryAfterSeconds !== null
            ? `Please wait ${formatWait(error.retryAfterSeconds)} before trying again.`
            : 'Please wait a moment before trying again.',
      };
    case 'invalid_response':
      return { title: 'Unexpected response', message: FALLBACK_MESSAGES.invalid_response };
    case 'unauthenticated':
      return { title: 'Signed out', message: 'Your session has ended. Please sign in again.' };
    case 'not_found':
      return { title: 'Not found', message: error.message || FALLBACK_MESSAGES.not_found };
    default:
      return { title: 'Request failed', message: error.message || FALLBACK_MESSAGES[error.code] };
  }
}

/** Errors worth retrying automatically: no response, timeouts and gateway/availability failures. */
export function isTransientError(error: unknown): boolean {
  if (!isApiError(error)) return false;
  return (
    error.code === 'network_error' ||
    error.code === 'timeout' ||
    error.status === 502 ||
    error.status === 503 ||
    error.status === 504
  );
}
