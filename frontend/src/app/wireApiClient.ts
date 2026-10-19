import { configureApiClient } from '../api/client';
import { sessionStore } from '../auth/sessionStore';

/**
 * Connects the API client to the session: requests carry the current bearer token, and a
 * 401 for a token that is still the active one ends the session (the router then redirects
 * to /sign-in with an explanation). Called once at start-up, before the first render.
 */
export function wireApiClient(): void {
  configureApiClient({
    getToken: () => sessionStore.getToken(),
    onUnauthorized: (token) => {
      sessionStore.rejectToken(token);
    },
  });
}
