import { screen } from '@testing-library/react';
import { describe, expect, it } from 'vitest';
import { me } from '../../test/fixtures';
import { backend, errorResponse, route } from '../../test/mockBackend';
import { renderApp } from '../../test/render';
import { server } from '../../test/server';
import { profilePolling } from './useMyProfile';

const PROFILE = '*/api/v1/me/profile';

/** GET /me/profile answers 404 `times` times (profile still being provisioned), then normally. */
function provisioningFor(times: number) {
  let remaining = times;
  server.use(
    route('get', PROFILE, () => {
      if (remaining <= 0) return undefined;
      remaining -= 1;
      return errorResponse(404, 'not_found', 'Profile not found.');
    }),
  );
}

describe('ProfileGate', () => {
  it('polls while the new profile is provisioned and then shows the app', async () => {
    profilePolling.intervalMs = 60;
    provisioningFor(3);
    renderApp('/');

    expect(await screen.findByText('Setting up your profile…')).toBeInTheDocument();
    expect(screen.getByText(/adding you to the network/)).toBeInTheDocument();
    expect(
      await screen.findByRole('heading', { level: 1, name: 'Network overview' }),
    ).toBeInTheDocument();
    expect(screen.queryByText('Setting up your profile…')).not.toBeInTheDocument();
    // Three 404s, then the profile (the logged override and the default handler both record it).
    expect(backend.requests('/api/v1/me/profile').length).toBeGreaterThanOrEqual(4);
  });

  it('stops polling after a while and lets the member try again', async () => {
    profilePolling.attempts = 2;
    provisioningFor(3);
    const { user } = renderApp('/');

    expect(await screen.findByText("Your profile isn't ready yet")).toBeInTheDocument();
    await user.click(screen.getByRole('button', { name: 'Try again' }));
    expect(
      await screen.findByRole('heading', { level: 1, name: 'Network overview' }),
    ).toBeInTheDocument();
  });

  it('shows other failures with a retry', async () => {
    let failing = true;
    server.use(
      route('get', PROFILE, () =>
        failing ? errorResponse(500, 'internal', 'Database unavailable') : undefined,
      ),
    );
    const { user } = renderApp('/');
    expect(await screen.findByText("We couldn't load your profile")).toBeInTheDocument();
    expect(screen.getByText('Reference: req-test-1')).toBeInTheDocument();

    failing = false;
    await user.click(screen.getByRole('button', { name: 'Try again' }));
    expect(
      await screen.findByRole('heading', { level: 1, name: 'Network overview' }),
    ).toBeInTheDocument();
    expect(screen.getByRole('link', { name: new RegExp(me.name) })).toBeInTheDocument();
  });
});
