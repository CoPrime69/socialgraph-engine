import { describe, expect, it } from 'vitest';
import {
  ApiError,
  codeForStatus,
  describeError,
  errorFromResponse,
  hasStatus,
  isTransientError,
  parseErrorEnvelope,
  parseRetryAfter,
} from './errors';

describe('parseRetryAfter', () => {
  it('reads delta-seconds', () => {
    expect(parseRetryAfter('30')).toBe(30);
    expect(parseRetryAfter(' 0 ')).toBe(0);
  });

  it('reads an HTTP date relative to now', () => {
    const now = Date.parse('2026-10-19T12:00:00Z');
    expect(parseRetryAfter('Mon, 19 Oct 2026 12:00:45 GMT', now)).toBe(45);
    expect(parseRetryAfter('Mon, 19 Oct 2026 11:00:00 GMT', now)).toBe(0);
  });

  it('ignores missing or malformed values', () => {
    expect(parseRetryAfter(null)).toBeNull();
    expect(parseRetryAfter('')).toBeNull();
    expect(parseRetryAfter('soon')).toBeNull();
  });
});

describe('parseErrorEnvelope', () => {
  it('parses the contract envelope and keeps only well-formed details', () => {
    const envelope = parseErrorEnvelope(
      JSON.stringify({
        error: {
          code: 'validation_failed',
          message: 'Invalid input',
          details: [{ field: 'handle', message: 'too short' }, { nope: true }, 'bad'],
        },
      }),
    );
    expect(envelope).toEqual({
      code: 'validation_failed',
      message: 'Invalid input',
      details: [{ field: 'handle', message: 'too short' }],
    });
  });

  it('rejects bodies that are not an envelope', () => {
    expect(parseErrorEnvelope('')).toBeNull();
    expect(parseErrorEnvelope('<html>502 Bad Gateway</html>')).toBeNull();
    expect(parseErrorEnvelope('{"message":"x"}')).toBeNull();
    expect(parseErrorEnvelope('{"error":{"message":"no code"}}')).toBeNull();
    expect(parseErrorEnvelope('[1,2]')).toBeNull();
  });

  it('tolerates a missing message and details', () => {
    expect(parseErrorEnvelope('{"error":{"code":"internal"}}')).toEqual({
      code: 'internal',
      message: '',
      details: [],
    });
  });
});

describe('codeForStatus', () => {
  it.each([
    [400, 'bad_request'],
    [401, 'unauthenticated'],
    [404, 'not_found'],
    [409, 'conflict'],
    [413, 'payload_too_large'],
    [422, 'validation_failed'],
    [429, 'rate_limited'],
    [502, 'unavailable'],
    [503, 'unavailable'],
    [504, 'unavailable'],
    [500, 'internal'],
    [418, 'http_error'],
  ] as const)('maps %i to %s', (status, code) => {
    expect(codeForStatus(status)).toBe(code);
  });
});

describe('errorFromResponse', () => {
  it('uses the envelope code, message, details, request id and Retry-After', async () => {
    const response = new Response(
      JSON.stringify({
        error: {
          code: 'rate_limited',
          message: 'Slow down',
          details: [{ field: 'handle', message: 'x' }],
        },
      }),
      { status: 429, headers: { 'Retry-After': '12', 'X-Request-Id': 'abc' } },
    );
    const error = await errorFromResponse(response);
    expect(error).toBeInstanceOf(ApiError);
    expect(error).toMatchObject({
      status: 429,
      code: 'rate_limited',
      message: 'Slow down',
      requestId: 'abc',
      retryAfterSeconds: 12,
      fromEnvelope: true,
    });
    expect(error.details).toEqual([{ field: 'handle', message: 'x' }]);
  });

  it('falls back to the status for an HTML gateway page', async () => {
    const response = new Response('<html><body>502 Bad Gateway</body></html>', {
      status: 502,
      headers: { 'Content-Type': 'text/html' },
    });
    const error = await errorFromResponse(response);
    expect(error).toMatchObject({ status: 502, code: 'unavailable', fromEnvelope: false });
    expect(error.message).toBe('The service is temporarily unavailable. (HTTP 502)');
  });

  it('maps unknown envelope codes by status and fills in an empty message', async () => {
    const response = new Response(JSON.stringify({ error: { code: 'teapot', message: '' } }), {
      status: 409,
    });
    const error = await errorFromResponse(response);
    expect(error.code).toBe('conflict');
    expect(error.message).toBe('That conflicts with the current state.');
  });
});

describe('describeError', () => {
  const make = (
    code: ApiError['code'],
    extra: Partial<{ status: number; retryAfterSeconds: number; message: string }> = {},
  ) =>
    new ApiError({
      status: extra.status ?? 400,
      code,
      message: extra.message ?? 'Server says no',
      ...extra,
    });

  it('explains connectivity and server problems without leaking details', () => {
    expect(describeError(make('network_error', { status: 0 })).title).toBe(
      "Can't reach the server",
    );
    expect(describeError(make('timeout', { status: 0 })).title).toBe(
      'The server is taking too long',
    );
    expect(describeError(make('unavailable', { status: 503 })).title).toBe(
      'Service temporarily unavailable',
    );
    expect(describeError(make('internal', { status: 500 })).message).toBe(
      'The error has been logged. Please try again.',
    );
    expect(describeError(make('invalid_response', { status: 200 })).title).toBe(
      'Unexpected response',
    );
    expect(describeError(make('unauthenticated', { status: 401 })).title).toBe('Signed out');
  });

  it('formats the rate-limit wait', () => {
    expect(describeError(make('rate_limited', { status: 429, retryAfterSeconds: 1 })).message).toBe(
      'Please wait 1 second before trying again.',
    );
    expect(
      describeError(make('rate_limited', { status: 429, retryAfterSeconds: 90 })).message,
    ).toBe('Please wait 2 minutes before trying again.');
    expect(describeError(make('rate_limited', { status: 429 })).message).toBe(
      'Please wait a moment before trying again.',
    );
  });

  it('uses the server message for other errors', () => {
    expect(describeError(make('conflict', { status: 409 }))).toEqual({
      title: 'Request failed',
      message: 'Server says no',
    });
    expect(describeError(make('not_found', { status: 404 })).message).toBe('Server says no');
  });

  it('describes non-API errors generically', () => {
    expect(describeError(new TypeError('x is undefined'))).toEqual({
      title: 'Something went wrong',
      message: 'An unexpected error occurred. Please try again.',
    });
  });
});

describe('error predicates', () => {
  it('treats only connectivity and gateway failures as transient', () => {
    expect(isTransientError(new ApiError({ status: 0, code: 'network_error', message: '' }))).toBe(
      true,
    );
    expect(isTransientError(new ApiError({ status: 0, code: 'timeout', message: '' }))).toBe(true);
    expect(isTransientError(new ApiError({ status: 503, code: 'unavailable', message: '' }))).toBe(
      true,
    );
    expect(isTransientError(new ApiError({ status: 500, code: 'internal', message: '' }))).toBe(
      false,
    );
    expect(isTransientError(new ApiError({ status: 404, code: 'not_found', message: '' }))).toBe(
      false,
    );
    expect(isTransientError(new Error('boom'))).toBe(false);
  });

  it('checks statuses only on ApiErrors', () => {
    expect(hasStatus(new ApiError({ status: 404, code: 'not_found', message: '' }), 404)).toBe(
      true,
    );
    expect(hasStatus({ status: 404 }, 404)).toBe(false);
  });
});
