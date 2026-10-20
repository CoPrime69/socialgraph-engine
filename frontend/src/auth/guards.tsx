import type { ReactNode } from 'react';
import { Navigate, useLocation, type Location } from 'react-router';
import { useAuth } from './authContext';
import { sessionStore } from './sessionStore';

export interface AuthRedirectState {
  from?: Pick<Location, 'pathname' | 'search' | 'hash'>;
}

function isAuthRoute(pathname: string): boolean {
  return pathname === '/sign-in' || pathname === '/register';
}

/**
 * Redirects anonymous visitors to /sign-in, remembering where they were going. A member who signed
 * out on purpose starts again from the overview, so nothing is remembered then.
 */
export function RequireAuth({ children }: { children: ReactNode }) {
  const { session } = useAuth();
  const location = useLocation();
  if (!session) {
    if (sessionStore.getLastSignOutReason() === 'signed_out') {
      return <Navigate to="/sign-in" replace />;
    }
    const state: AuthRedirectState = {
      from: { pathname: location.pathname, search: location.search, hash: location.hash },
    };
    return <Navigate to="/sign-in" replace state={state} />;
  }
  return children;
}

/** Sends signed-in users away from /sign-in and /register (back to where they came from). */
export function RedirectIfAuthenticated({ children }: { children: ReactNode }) {
  const { session } = useAuth();
  const location = useLocation();
  if (session) {
    const from = (location.state as AuthRedirectState | null)?.from;
    const target = from && !isAuthRoute(from.pathname) ? from : { pathname: '/' };
    return <Navigate to={target} replace />;
  }
  return children;
}
