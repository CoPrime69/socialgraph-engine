import { screen, waitFor, within } from '@testing-library/react';
import { http, HttpResponse } from 'msw';
import { describe, expect, it } from 'vitest';
import { axeViolations } from '../../test/axe';
import { summary } from '../../test/fixtures';
import { backend, errorResponse, route } from '../../test/mockBackend';
import { deferred, renderApp } from '../../test/render';
import { server } from '../../test/server';

async function openOverview() {
  const view = renderApp('/');
  await screen.findByRole('heading', { level: 1, name: 'Network overview' });
  return view;
}

function statistics() {
  return screen.findByLabelText('Network statistics');
}

describe('Overview', () => {
  it('shows skeletons while the statistics load', async () => {
    const release = deferred();
    server.use(
      http.get('*/api/v1/analytics/summary', async () => {
        await release.promise;
        return undefined;
      }),
    );
    await openOverview();
    expect(screen.getByText('Loading network statistics…')).toBeInTheDocument();
    release.resolve();
    expect(await screen.findByText('18,649')).toBeInTheDocument();
    expect(screen.queryByText('Loading network statistics…')).not.toBeInTheDocument();
  });

  it('summarises the network and when it was computed', async () => {
    await openOverview();
    const tiles = await statistics();
    expect(within(tiles).getByText('People').nextElementSibling).toHaveTextContent('18,649');
    expect(within(tiles).getByText('Connections').nextElementSibling).toHaveTextContent('204,670');
    expect(within(tiles).getByText('22.0 per person on average')).toBeInTheDocument();
    expect(within(tiles).getByText('Modularity 0.47')).toBeInTheDocument();
    expect(within(tiles).getByText('Largest connected group').nextElementSibling).toHaveTextContent(
      '99.6%',
    );
    expect(within(tiles).getByText('18,571 people · 37 separate groups')).toBeInTheDocument();
    expect(screen.getByText(/Graph version 41 · analytics computed/)).toHaveTextContent(
      /in 2,840 ms$/,
    );
  });

  it('shows an error with a working retry when the statistics fail', async () => {
    let fail = true;
    server.use(
      route('get', '*/api/v1/analytics/summary', () =>
        fail ? errorResponse(500, 'internal', 'Read model unavailable') : undefined,
      ),
    );
    const { user } = await openOverview();
    expect(await screen.findByText('Network statistics are unavailable')).toBeInTheDocument();

    fail = false;
    await user.click(screen.getAllByRole('button', { name: 'Try again' })[0]!);
    expect(await screen.findByText('18,649')).toBeInTheDocument();
  });

  it('ranks influencers and switches the measure', async () => {
    const { user } = await openOverview();
    const table = await screen.findByRole('table', { name: 'Most influential people by PageRank' });
    const rows = within(table).getAllByRole('row').slice(1);
    expect(rows).toHaveLength(4);
    expect(rows[0]).toHaveTextContent('#1');
    expect(rows[0]).toHaveTextContent('Jon Skeet');
    expect(rows[0]).toHaveTextContent('1,457,162');
    expect(within(rows[0]!).getByRole('meter')).toHaveAttribute('aria-valuetext', '0.00382');
    expect(backend.lastRequest('/api/v1/analytics/influencers')?.query.get('metric')).toBe(
      'pagerank',
    );

    await user.click(screen.getByRole('radio', { name: 'Betweenness' }));
    expect(
      await screen.findByRole('table', { name: 'Most influential people by Betweenness' }),
    ).toBeInTheDocument();
    await waitFor(() => {
      expect(screen.getByRole('columnheader', { name: 'Betweenness' })).toBeInTheDocument();
    });
    expect(backend.lastRequest('/api/v1/analytics/influencers')?.query.get('metric')).toBe(
      'betweenness',
    );
    expect(screen.getByText(/Estimated from a sample of the network\./)).toBeInTheDocument();

    await user.click(screen.getByRole('radio', { name: 'Connections' }));
    await waitFor(() => {
      expect(screen.getByRole('columnheader', { name: 'Connections' })).toBeInTheDocument();
    });
    const degreeTable = screen.getByRole('table', {
      name: 'Most influential people by Connections',
    });
    expect(within(degreeTable).getAllByRole('meter')[0]).toHaveAttribute('aria-valuetext', '6.47%');
  });

  it('explains an empty ranking and a failed one', async () => {
    server.use(
      http.get('*/api/v1/analytics/influencers', () =>
        HttpResponse.json({ graph_version: 41, metric: 'pagerank', items: [] }),
      ),
    );
    const { user } = await openOverview();
    expect(await screen.findByText('No one to rank yet')).toBeInTheDocument();

    server.use(
      http.get('*/api/v1/analytics/influencers', () =>
        errorResponse(503, 'unavailable', 'Read model not bootstrapped'),
      ),
    );
    await user.click(screen.getByRole('radio', { name: 'Closeness' }));
    expect(await screen.findByText('Service temporarily unavailable')).toBeInTheDocument();
  });

  it('lists the largest communities with links to them', async () => {
    await openOverview();
    const card = await screen.findByRole('region', { name: 'Largest communities' });
    const links = await within(card).findAllByRole('link', { name: /Community/ });
    expect(links[0]).toHaveAttribute('href', '/communities?highlight=3');
    expect(links[0]).toHaveTextContent('c# · .NET & Windows');
    expect(links[0]).toHaveTextContent('22.6%');
    expect(within(card).getByRole('link', { name: 'See all communities' })).toHaveAttribute(
      'href',
      '/communities',
    );
  });

  it('draws the network graph and offers the same people as a table', async () => {
    const { user } = await openOverview();
    const card = screen.getByRole('region', { name: 'Network graph' });
    const canvas = await within(card).findByRole('img', {
      name: /most influential of 18,649 people/,
    });
    expect(canvas).toHaveAccessibleDescription(/Drag to pan/);
    expect(within(card).getByText('Graph version 41')).toBeInTheDocument();
    expect(backend.lastRequest('/api/v1/analytics/graph')?.query.get('limit')).toBe('1500');

    await user.selectOptions(within(card).getByLabelText('People shown'), '500');
    await waitFor(() => {
      expect(backend.lastRequest('/api/v1/analytics/graph')?.query.get('limit')).toBe('500');
    });

    await user.click(within(card).getByRole('radio', { name: 'Table' }));
    const table = within(card).getByRole('table', { name: /People in the graph, by PageRank/ });
    const rows = within(table).getAllByRole('row').slice(1);
    expect(rows).toHaveLength(8);
    expect(rows[0]).toHaveTextContent('Jon Skeet');

    // Highlighting a community in the legend narrows the table to its members.
    const legendButton = within(card).getByRole('button', { name: /Community 0/ });
    await user.click(legendButton);
    expect(legendButton).toHaveAttribute('aria-pressed', 'true');
    expect(within(table).getAllByRole('row').slice(1)).toHaveLength(3);
    await user.click(legendButton);
    expect(legendButton).toHaveAttribute('aria-pressed', 'false');
  });

  it('has no detectable accessibility violations', async () => {
    const { container } = await openOverview();
    await screen.findByText('18,649');
    await screen.findByRole('table', { name: /Most influential people/ });
    expect(await axeViolations(container)).toEqual([]);
  });

  it('uses the summary fixture', () => {
    // Guards the arithmetic above: 18,571 of 18,649 people are in the largest component.
    expect(summary.largest_component / summary.users).toBeCloseTo(0.9958, 4);
  });
});
