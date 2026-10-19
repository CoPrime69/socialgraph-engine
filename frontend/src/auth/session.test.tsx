import { act, screen, waitFor } from '@testing-library/react';
import { describe, expect, it } from 'vitest';
import { authResponse } from '../test/fixtures';
import { backend } from '../test/mockBackend';
import { renderApp } from '../test/render';
import { SESSION_STORAGE_KEY, sessionStore } from './sessionStore';

describe('Protected routes and the session lifecycle', () => {
  it('sends anonymous visitors to the sign-in page', async () => {
    const { router } = renderApp('/network', { signedIn: false });
    expect(await screen.findByRole('heading', { level: 1, name: 'Sign in' })).toBeInTheDocument();
    expect(router.state.location.pathname).toBe('/sign-in');
    expect(backend.requests('/api/v1/me/connections')).toHaveLength(0);
  });

  it('signs out automatically when the API rejects the token', async () => {
    const { router } = renderApp('/');
    await screen.findByRole('heading', { level: 1, name: 'Network overview' });

    // The token is revoked server-side (e.g. the signing key rotated); the next request fails.
    backend.state.tokens.clear();
    await act(async () => {
      await router.navigate('/network');
    });

    expect(await screen.findByRole('heading', { level: 1, name: 'Sign in' })).toBeInTheDocument();
    expect(
      screen.getByText('Your session is no longer valid. Please sign in again.'),
    ).toBeInTheDocument();
    expect(window.localStorage.getItem(SESSION_STORAGE_KEY)).toBeNull();
  });

  it('signs out from the header and confirms it', async () => {
    const { user, queryClient } = renderApp('/');
    await screen.findByRole('heading', { level: 1, name: 'Network overview' });
    await user.click(screen.getByRole('button', { name: 'Sign out' }));

    expect(await screen.findByText('You have been signed out.')).toBeInTheDocument();
    expect(sessionStore.getSnapshot()).toBeNull();
    // Nothing from the previous user stays cached.
    await waitFor(() => {
      expect(queryClient.getQueryCache().getAll()).toHaveLength(0);
    });
  });

  it('ends the session when the token expires', async () => {
    sessionStore.start({ ...authResponse, expires_at: new Date(Date.now() + 1500).toISOString() });
    renderApp('/', { signedIn: false });
    await screen.findByRole('heading', { level: 1, name: 'Network overview' });
    expect(
      await screen.findByText('Your session expired. Please sign in again.', {}, { timeout: 4000 }),
    ).toBeInTheDocument();
  });

  it('follows a sign-out in another tab', async () => {
    renderApp('/');
    await screen.findByRole('heading', { level: 1, name: 'Network overview' });
    act(() => {
      window.localStorage.removeItem(SESSION_STORAGE_KEY);
      window.dispatchEvent(new StorageEvent('storage', { key: SESSION_STORAGE_KEY }));
    });
    expect(await screen.findByRole('heading', { level: 1, name: 'Sign in' })).toBeInTheDocument();
  });

  it('restores the session after a reload', async () => {
    sessionStore.start(authResponse);
    sessionStore.resetForTests(); // simulates a new page load: the session comes from storage
    renderApp('/network', { signedIn: false });
    expect(
      await screen.findByRole('heading', { level: 1, name: 'My network' }),
    ).toBeInTheDocument();
  });
});
