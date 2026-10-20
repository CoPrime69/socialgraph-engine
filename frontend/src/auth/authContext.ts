import { createContext, useContext } from 'react';
import type { AuthResponse } from '../api/types';
import type { Session } from './sessionStore';

export interface AuthContextValue {
  session: Session | null;
  signIn: (auth: AuthResponse) => void;
  signOut: () => void;
}

export const AuthContext = createContext<AuthContextValue | null>(null);

export function useAuth(): AuthContextValue {
  const context = useContext(AuthContext);
  if (!context) throw new Error('useAuth must be used inside <AuthProvider>');
  return context;
}

/** The signed-in session; only valid below <RequireAuth>. */
export function useSession(): Session {
  const { session } = useAuth();
  if (!session) throw new Error('useSession requires an authenticated route');
  return session;
}
