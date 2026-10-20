import { useQueryClient } from '@tanstack/react-query';
import {
  useCallback,
  useEffect,
  useMemo,
  useRef,
  useSyncExternalStore,
  type ReactNode,
} from 'react';
import { graphVersionStore } from '../api/graphVersion';
import type { AuthResponse } from '../api/types';
import { AuthContext } from './authContext';
import { SESSION_STORAGE_KEY, sessionStore } from './sessionStore';

/** setTimeout cannot represent delays above ~24.8 days. */
const MAX_TIMEOUT_MS = 2 ** 31 - 1;

export function AuthProvider({ children }: { children: ReactNode }) {
  const queryClient = useQueryClient();
  const session = useSyncExternalStore(
    sessionStore.subscribe,
    sessionStore.getSnapshot,
    () => null,
  );

  // Whenever the session ends (sign-out, expiry, 401, another tab), drop every cached
  // response so nothing from the previous user leaks into the next one.
  const previousToken = useRef<string | null>(session?.token ?? null);
  useEffect(() => {
    const token = session?.token ?? null;
    if (previousToken.current !== null && previousToken.current !== token) {
      queryClient.clear();
      graphVersionStore.reset();
    }
    previousToken.current = token;
  }, [session, queryClient]);

  // End the session when the token expires.
  useEffect(() => {
    if (!session) return;
    const remaining = Date.parse(session.expiresAt) - Date.now();
    const timer = setTimeout(
      () => {
        sessionStore.end('expired');
      },
      Math.min(Math.max(remaining, 0), MAX_TIMEOUT_MS),
    );
    return () => {
      clearTimeout(timer);
    };
  }, [session]);

  // Keep tabs in sync: signing out in one tab signs out the others.
  useEffect(() => {
    const onStorage = (event: StorageEvent) => {
      if (event.key === null || event.key === SESSION_STORAGE_KEY) sessionStore.syncFromStorage();
    };
    window.addEventListener('storage', onStorage);
    return () => {
      window.removeEventListener('storage', onStorage);
    };
  }, []);

  const signIn = useCallback((auth: AuthResponse) => {
    sessionStore.start(auth);
  }, []);

  const signOut = useCallback(() => {
    sessionStore.end('signed_out');
  }, []);

  const value = useMemo(() => ({ session, signIn, signOut }), [session, signIn, signOut]);
  return <AuthContext.Provider value={value}>{children}</AuthContext.Provider>;
}
