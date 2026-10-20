import type { AuthResponse, AuthUser } from '../api/types';

/**
 * The signed-in session, persisted in localStorage so it survives reloads and is shared by
 * tabs (see README "Token storage" for the trade-off). It is an external store consumed with
 * `useSyncExternalStore`, and is also read synchronously by the API client.
 */

export interface Session {
  token: string;
  /** RFC 3339 expiry reported by identity-service. */
  expiresAt: string;
  user: AuthUser;
}

export type SignOutReason = 'signed_out' | 'expired' | 'unauthorized';

export const SESSION_STORAGE_KEY = 'socialgraph.session.v1';

type Listener = () => void;

let current: Session | null = null;
let lastSignOutReason: SignOutReason | null = null;
let initialised = false;
const listeners = new Set<Listener>();

function storage(): Storage | null {
  try {
    return window.localStorage;
  } catch {
    return null; // Storage disabled (privacy mode / sandboxed iframe).
  }
}

function isSession(value: unknown): value is Session {
  if (typeof value !== 'object' || value === null) return false;
  const candidate = value as { token?: unknown; expiresAt?: unknown; user?: unknown };
  if (typeof candidate.user !== 'object' || candidate.user === null) return false;
  const user = candidate.user as { id?: unknown; handle?: unknown; name?: unknown };
  return (
    typeof candidate.token === 'string' &&
    candidate.token !== '' &&
    typeof candidate.expiresAt === 'string' &&
    typeof user.id === 'number' &&
    typeof user.handle === 'string' &&
    typeof user.name === 'string'
  );
}

export function isExpired(session: Session, now: number = Date.now()): boolean {
  const expiry = Date.parse(session.expiresAt);
  return Number.isNaN(expiry) || expiry <= now;
}

function readPersisted(): Session | null {
  const raw = storage()?.getItem(SESSION_STORAGE_KEY);
  if (!raw) return null;
  try {
    const parsed: unknown = JSON.parse(raw);
    if (isSession(parsed) && !isExpired(parsed)) return parsed;
  } catch {
    // Corrupted value: fall through and discard it.
  }
  storage()?.removeItem(SESSION_STORAGE_KEY);
  return null;
}

function emit(): void {
  for (const listener of listeners) listener();
}

function ensureInitialised(): void {
  if (initialised) return;
  initialised = true;
  current = readPersisted();
}

export const sessionStore = {
  /** Pure snapshot for useSyncExternalStore (expiry is enforced by AuthProvider's timer). */
  // Arrow functions: passed by reference to useSyncExternalStore.
  getSnapshot: (): Session | null => {
    ensureInitialised();
    return current;
  },

  /** Token for outgoing requests; an already-expired token is not sent. */
  getToken(): string | null {
    ensureInitialised();
    if (!current || isExpired(current)) return null;
    return current.token;
  },

  /** Reason for the most recent automatic or manual sign-out (shown on the sign-in page). */
  getLastSignOutReason(): SignOutReason | null {
    return lastSignOutReason;
  },

  clearSignOutReason(): void {
    lastSignOutReason = null;
  },

  start(auth: AuthResponse): Session {
    ensureInitialised();
    const session: Session = { token: auth.token, expiresAt: auth.expires_at, user: auth.user };
    current = session;
    lastSignOutReason = null;
    try {
      storage()?.setItem(SESSION_STORAGE_KEY, JSON.stringify(session));
    } catch {
      // Quota/storage errors: the session still works for this tab.
    }
    emit();
    return session;
  },

  end(reason: SignOutReason): void {
    ensureInitialised();
    if (current === null) return;
    current = null;
    lastSignOutReason = reason;
    storage()?.removeItem(SESSION_STORAGE_KEY);
    emit();
  },

  /** Ends the session only if `token` is still the active one (ignores late 401s for old tokens). */
  rejectToken(token: string): void {
    if (current?.token === token) sessionStore.end('unauthorized');
  },

  subscribe: (listener: Listener): (() => void) => {
    listeners.add(listener);
    return () => {
      listeners.delete(listener);
    };
  },

  /** Applies a change made by another tab (the `storage` event). */
  syncFromStorage(): void {
    const next = readPersisted();
    const changed = next?.token !== current?.token;
    if (!changed) return;
    if (next === null && current !== null) lastSignOutReason = 'signed_out';
    current = next;
    emit();
  },

  /** Test helper: forget in-memory state so the next read comes from localStorage again. */
  resetForTests(): void {
    current = null;
    lastSignOutReason = null;
    initialised = false;
    listeners.clear();
  },
};
