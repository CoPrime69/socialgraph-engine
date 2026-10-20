import { render, screen } from '@testing-library/react';
import { describe, expect, it } from 'vitest';
import { sessionStore } from '../auth/sessionStore';
import { authResponse } from '../test/fixtures';
import { App } from './App';

// The real entry component: browser router, production query client and lazily loaded pages.
describe('App', () => {
  it('starts visitors at the sign-in page', async () => {
    render(<App />);
    expect(await screen.findByRole('heading', { level: 1, name: 'Sign in' })).toBeInTheDocument();
    expect(window.location.pathname).toBe('/sign-in');
  });

  it('restores a saved session and opens the overview', async () => {
    sessionStore.start(authResponse);
    render(<App />);
    expect(
      await screen.findByRole('heading', { level: 1, name: 'Network overview' }),
    ).toBeInTheDocument();
    expect(window.location.pathname).toBe('/');
  });
});
