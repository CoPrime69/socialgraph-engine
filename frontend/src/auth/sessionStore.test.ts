import { describe, expect, it, vi } from 'vitest';
import { authResponse } from '../test/fixtures';
import { isExpired, SESSION_STORAGE_KEY, sessionStore } from './sessionStore';

function persisted(): unknown {
  const raw = window.localStorage.getItem(SESSION_STORAGE_KEY);
  return raw === null ? null : JSON.parse(raw);
}

describe('sessionStore', () => {
  it('persists a new session and exposes its token', () => {
    const listener = vi.fn();
    sessionStore.subscribe(listener);
    const session = sessionStore.start(authResponse);
    expect(session).toEqual({
      token: 'test-token',
      expiresAt: authResponse.expires_at,
      user: authResponse.user,
    });
    expect(sessionStore.getSnapshot()).toBe(session);
    expect(sessionStore.getToken()).toBe('test-token');
    expect(persisted()).toEqual(session);
    expect(listener).toHaveBeenCalledTimes(1);
  });

  it('restores a stored session after a reload', () => {
    sessionStore.start(authResponse);
    sessionStore.resetForTests();
    expect(sessionStore.getSnapshot()?.user.handle).toBe('ada_member');
  });

  it('discards stored sessions that are expired or malformed', () => {
    window.localStorage.setItem(
      SESSION_STORAGE_KEY,
      JSON.stringify({ token: 't', expiresAt: '2000-01-01T00:00:00Z', user: authResponse.user }),
    );
    expect(sessionStore.getSnapshot()).toBeNull();
    expect(window.localStorage.getItem(SESSION_STORAGE_KEY)).toBeNull();

    sessionStore.resetForTests();
    window.localStorage.setItem(SESSION_STORAGE_KEY, '{not json');
    expect(sessionStore.getSnapshot()).toBeNull();

    sessionStore.resetForTests();
    window.localStorage.setItem(SESSION_STORAGE_KEY, JSON.stringify({ token: 't', user: null }));
    expect(sessionStore.getSnapshot()).toBeNull();
  });

  it('does not send a token that has expired in the meantime', () => {
    sessionStore.start({ ...authResponse, expires_at: '2000-01-01T00:00:00Z' });
    expect(sessionStore.getToken()).toBeNull();
    expect(isExpired({ token: 't', expiresAt: 'not a date', user: authResponse.user })).toBe(true);
  });

  it('records why the session ended', () => {
    sessionStore.start(authResponse);
    sessionStore.end('expired');
    expect(sessionStore.getSnapshot()).toBeNull();
    expect(sessionStore.getLastSignOutReason()).toBe('expired');
    expect(persisted()).toBeNull();
    sessionStore.end('signed_out'); // already ended: no change
    expect(sessionStore.getLastSignOutReason()).toBe('expired');
    sessionStore.clearSignOutReason();
    expect(sessionStore.getLastSignOutReason()).toBeNull();
  });

  it('ends the session only for a 401 on the current token', () => {
    sessionStore.start(authResponse);
    sessionStore.rejectToken('an-older-token');
    expect(sessionStore.getSnapshot()).not.toBeNull();
    sessionStore.rejectToken('test-token');
    expect(sessionStore.getSnapshot()).toBeNull();
    expect(sessionStore.getLastSignOutReason()).toBe('unauthorized');
  });

  it('follows sign-in and sign-out in another tab', () => {
    const listener = vi.fn();
    sessionStore.getSnapshot();
    sessionStore.subscribe(listener);

    window.localStorage.setItem(
      SESSION_STORAGE_KEY,
      JSON.stringify({
        token: 'other',
        expiresAt: authResponse.expires_at,
        user: authResponse.user,
      }),
    );
    sessionStore.syncFromStorage();
    expect(sessionStore.getToken()).toBe('other');

    sessionStore.syncFromStorage(); // unchanged
    expect(listener).toHaveBeenCalledTimes(1);

    window.localStorage.removeItem(SESSION_STORAGE_KEY);
    sessionStore.syncFromStorage();
    expect(sessionStore.getSnapshot()).toBeNull();
    expect(sessionStore.getLastSignOutReason()).toBe('signed_out');
    expect(listener).toHaveBeenCalledTimes(2);
  });

  it('keeps working when storage is unavailable', () => {
    vi.spyOn(Storage.prototype, 'setItem').mockImplementation(() => {
      throw new DOMException('Quota exceeded', 'QuotaExceededError');
    });
    expect(sessionStore.start(authResponse).token).toBe('test-token');
    expect(sessionStore.getToken()).toBe('test-token');
  });
});
