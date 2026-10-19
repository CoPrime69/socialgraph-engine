import { screen, waitFor, within } from '@testing-library/react';
import { http, HttpResponse } from 'msw';
import { describe, expect, it } from 'vitest';
import { axeViolations } from '../../test/axe';
import { christian, importedConnection, jonSkeet, quentin } from '../../test/fixtures';
import { backend, errorResponse } from '../../test/mockBackend';
import { deferred, renderApp } from '../../test/render';
import { server } from '../../test/server';

const CONNECTIONS = '*/api/v1/me/connections';

async function openMyNetwork() {
  const view = renderApp('/network');
  await screen.findByRole('heading', { level: 1, name: 'My network' });
  const connections = screen.getByRole('region', { name: 'Your connections' });
  const suggestions = screen.getByRole('region', { name: 'People you may know' });
  await within(connections).findByRole('link', { name: /Quentin/ });
  await within(suggestions).findByRole('button', { name: `Connect with ${christian.name}` });
  return { ...view, connections, suggestions };
}

/** People listed in "Your connections", in order (each row has a labelled strength meter). */
function names(region: HTMLElement): string[] {
  return within(region)
    .queryAllByRole('meter')
    .map(
      (meter) => meter.getAttribute('aria-label')?.replace('Connection strength with ', '') ?? '',
    );
}

describe('My network', () => {
  it('lists connections by strength with when they were made', async () => {
    const { connections } = await openMyNetwork();
    expect(
      within(connections).getByText('2 people, strongest connection first'),
    ).toBeInTheDocument();
    expect(names(connections)).toEqual(['Quentin', 'bobince']);
    expect(
      within(connections).getByRole('meter', { name: 'Connection strength with Quentin' }),
    ).toHaveAttribute('aria-valuetext', '87%');
    expect(within(connections).getAllByText('Connected Oct 19, 2026')).toHaveLength(2);
  });

  it('explains why each person is suggested', async () => {
    const { suggestions } = await openMyNetwork();
    const reasons = within(suggestions).getByRole('list', {
      name: `Why ${christian.name} is suggested`,
    });
    expect(
      within(reasons)
        .getAllByRole('listitem')
        .map((item) => item.textContent),
    ).toEqual(['2 mutual connections', 'In your community', 'Also works with javascript']);
    expect(
      within(suggestions).getByRole('list', { name: `Why ${jonSkeet.name} is suggested` }),
    ).toHaveTextContent('Profile similarity 30%');
    expect(
      within(suggestions).getByRole('meter', { name: `Match with ${christian.name}` }),
    ).toHaveAttribute('aria-valuetext', '81%');
  });

  it('connects with a suggestion: pending state, immediate update and read-your-writes refetch', async () => {
    const release = deferred();
    server.use(
      http.post(CONNECTIONS, async () => {
        await release.promise;
        return undefined;
      }),
    );
    const { user, connections, suggestions } = await openMyNetwork();
    const connect = within(suggestions).getByRole('button', {
      name: `Connect with ${christian.name}`,
    });
    await user.click(connect);

    expect(connect).toBeDisabled();
    expect(connect).toHaveAttribute('aria-busy', 'true');
    expect(connect).toHaveTextContent('Connecting…');
    expect(
      within(suggestions).getByRole('button', { name: `Connect with ${jonSkeet.name}` }),
    ).toBeEnabled();

    release.resolve();
    expect(await screen.findByText(`Connected with ${christian.name}`)).toBeInTheDocument();
    await waitFor(() => {
      expect(names(connections)).toHaveLength(3);
    });
    expect(names(connections)).toEqual(['Quentin', 'bobince', christian.name]);
    expect(
      within(suggestions).queryByRole('button', { name: `Connect with ${christian.name}` }),
    ).not.toBeInTheDocument();
    expect(backend.lastRequest('/api/v1/me/connections', 'POST')?.body).toEqual({
      user_id: christian.id,
    });

    // Analytics is refetched with the version returned by the change.
    await waitFor(() => {
      expect(backend.lastRequest('/api/v1/me/recommendations')?.query.get('min_version')).toBe(
        '42',
      );
    });
    await waitFor(() => {
      expect(backend.lastRequest('/api/v1/analytics/graph')?.query.get('min_version')).toBe('42');
    });
    expect(backend.lastRequest('/api/v1/analytics/graph')?.query.get('focus')).toBe('100000123');
  });

  it('shows an error toast when connecting fails and keeps the suggestion', async () => {
    server.use(http.post(CONNECTIONS, () => errorResponse(500, 'internal', 'Write failed')));
    const { user, connections, suggestions } = await openMyNetwork();
    const connect = within(suggestions).getByRole('button', {
      name: `Connect with ${christian.name}`,
    });
    await user.click(connect);

    const toast = await screen.findByText(`Couldn't connect with ${christian.name}`);
    expect(toast.closest('[role="alert"]')).not.toBeNull();
    // Server-side details of a 500 are not shown; the member gets actionable copy instead.
    expect(screen.getByText('The error has been logged. Please try again.')).toBeInTheDocument();
    expect(connect).toBeEnabled();
    expect(names(connections)).toHaveLength(2);

    await user.click(screen.getAllByRole('button', { name: 'Dismiss notification' })[0]!);
    expect(screen.queryByText(`Couldn't connect with ${christian.name}`)).not.toBeInTheDocument();
  });

  it('treats "already connected" as information and refreshes', async () => {
    server.use(http.post(CONNECTIONS, () => errorResponse(409, 'conflict', 'Already connected')));
    const { user, suggestions } = await openMyNetwork();
    const before = backend.requests('/api/v1/me/connections', 'GET').length;
    await user.click(
      within(suggestions).getByRole('button', { name: `Connect with ${christian.name}` }),
    );
    expect(
      await screen.findByText(`You're already connected with ${christian.name}`),
    ).toBeInTheDocument();
    await waitFor(() => {
      expect(backend.requests('/api/v1/me/connections', 'GET').length).toBeGreaterThan(before);
    });
  });

  it('removes a connection only after confirmation', async () => {
    const release = deferred();
    const { user, connections } = await openMyNetwork();

    await user.click(
      within(connections).getByRole('button', { name: 'Remove connection with Quentin' }),
    );
    let dialog = screen.getByRole('dialog', { name: 'Remove Quentin from your network?' });
    expect(within(dialog).getByRole('button', { name: 'Cancel' })).toHaveFocus();
    await user.click(within(dialog).getByRole('button', { name: 'Cancel' }));
    expect(screen.queryByRole('dialog')).not.toBeInTheDocument();
    expect(backend.requests(`/api/v1/me/connections/${String(quentin.id)}`, 'DELETE')).toHaveLength(
      0,
    );
    expect(names(connections)).toHaveLength(2);

    server.use(
      http.delete(`${CONNECTIONS}/:userId`, async () => {
        await release.promise;
        return undefined;
      }),
    );
    await user.click(
      within(connections).getByRole('button', { name: 'Remove connection with Quentin' }),
    );
    dialog = screen.getByRole('dialog', { name: 'Remove Quentin from your network?' });
    await user.click(within(dialog).getByRole('button', { name: 'Remove connection' }));
    expect(within(dialog).getByRole('button', { name: 'Removing…' })).toBeDisabled();
    expect(within(dialog).getByRole('button', { name: 'Cancel' })).toBeDisabled();

    release.resolve();
    expect(await screen.findByText('Removed your connection with Quentin')).toBeInTheDocument();
    await waitFor(() => {
      expect(names(connections)).toEqual(['bobince']);
    });
    expect(screen.queryByRole('dialog')).not.toBeInTheDocument();
    expect(backend.requests(`/api/v1/me/connections/${String(quentin.id)}`, 'DELETE')).toHaveLength(
      1,
    );
    await waitFor(() => {
      expect(backend.lastRequest('/api/v1/me/recommendations')?.query.get('min_version')).toBe(
        '42',
      );
    });
  });

  it('closes the confirmation with Escape', async () => {
    const { user, connections } = await openMyNetwork();
    await user.click(
      within(connections).getByRole('button', { name: 'Remove connection with Quentin' }),
    );
    const dialog = screen.getByRole('dialog');
    dialog.dispatchEvent(new Event('cancel', { cancelable: true }));
    await waitFor(() => {
      expect(screen.queryByRole('dialog')).not.toBeInTheDocument();
    });
  });

  it('reports a connection that was already removed elsewhere', async () => {
    server.use(
      http.delete(`${CONNECTIONS}/:userId`, () => errorResponse(404, 'not_found', 'Not connected')),
    );
    const { user, connections } = await openMyNetwork();
    await user.click(
      within(connections).getByRole('button', { name: 'Remove connection with Quentin' }),
    );
    await user.click(
      within(screen.getByRole('dialog')).getByRole('button', { name: 'Remove connection' }),
    );
    expect(await screen.findByText("You're no longer connected with Quentin")).toBeInTheDocument();
  });

  it('shows imported Stack Overflow connections by their interactions', async () => {
    server.use(
      http.get(CONNECTIONS, () =>
        HttpResponse.json({ items: [importedConnection], graph_version: 41 }),
      ),
    );
    renderApp('/network');
    const connections = await screen.findByRole('region', { name: 'Your connections' });
    expect(
      await within(connections).findByText('73 interactions since Sep 2008'),
    ).toBeInTheDocument();
    expect(
      within(connections).getByText('1 person, strongest connection first'),
    ).toBeInTheDocument();
  });

  it('guides members without connections', async () => {
    server.use(http.get(CONNECTIONS, () => HttpResponse.json({ items: [], graph_version: 41 })));
    renderApp('/network');
    expect(await screen.findByText("You haven't connected with anyone yet")).toBeInTheDocument();
    expect(screen.getByRole('link', { name: 'Find people' })).toHaveAttribute('href', '/people');
  });

  it('explains that suggestions need a moment for brand-new members', async () => {
    server.use(
      http.get('*/api/v1/me/recommendations', () =>
        errorResponse(404, 'not_found', 'Unknown user'),
      ),
    );
    renderApp('/network');
    expect(await screen.findByText('Your network is still being set up')).toBeInTheDocument();
  });

  it('says when there is no one left to suggest', async () => {
    server.use(
      http.get('*/api/v1/me/recommendations', () =>
        HttpResponse.json({ graph_version: 41, items: [] }),
      ),
    );
    renderApp('/network');
    expect(await screen.findByText('No suggestions right now')).toBeInTheDocument();
  });

  it('marks suggestions as updating until analytics has caught up', async () => {
    backend.state.analyticsPaused = true;
    const { user, suggestions } = await openMyNetwork();
    await user.click(
      within(suggestions).getByRole('button', { name: `Connect with ${christian.name}` }),
    );
    expect(
      await within(suggestions).findByText('Updating with your latest changes…'),
    ).toBeInTheDocument();

    backend.resumeAnalytics();
    await waitFor(
      () => {
        expect(
          within(suggestions).queryByText('Updating with your latest changes…'),
        ).not.toBeInTheDocument();
      },
      { timeout: 6000 },
    );
  });

  it('draws the member’s own network', async () => {
    await openMyNetwork();
    const card = screen.getByRole('region', { name: 'Your network graph' });
    expect(
      await within(card).findByRole('img', { name: /Ada Member and 2 direct connections/ }),
    ).toBeInTheDocument();
    expect(within(card).getByText(/The ringed circle is you\./)).toBeInTheDocument();
  });

  it('has no detectable accessibility violations', async () => {
    const { container } = await openMyNetwork();
    expect(await axeViolations(container)).toEqual([]);
  });
});
