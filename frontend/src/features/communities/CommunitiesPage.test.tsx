import { screen, waitFor, within } from '@testing-library/react';
import { http, HttpResponse } from 'msw';
import { describe, expect, it, vi } from 'vitest';
import type { Community } from '../../api/types';
import { axeViolations } from '../../test/axe';
import { communities } from '../../test/fixtures';
import { setMediaQuery } from '../../test/domShims';
import { errorResponse, route } from '../../test/mockBackend';
import { renderApp } from '../../test/render';
import { server } from '../../test/server';

const COMMUNITIES = '*/api/v1/analytics/communities';

async function openCommunities(path = '/communities') {
  const view = renderApp(path);
  await screen.findByRole('heading', { level: 1, name: 'Communities' });
  await screen.findByRole('article', { name: 'Community 3' });
  const graph = screen.getByRole('region', { name: 'Communities in the network' });
  await within(graph).findByRole('img', { name: /most influential of 18,649 people/ });
  return { ...view, graph };
}

function card(name: string) {
  return screen.getByRole('article', { name });
}

function highlightButton(name: string) {
  return within(card(name)).getByRole('button', { name: /in graph$/ });
}

function legendButton(graph: HTMLElement, name: string) {
  return within(graph).getByRole('button', { name: new RegExp(`^${name} `) });
}

describe('Communities', () => {
  it('lists every community by size with its share, technology and members', async () => {
    await openCommunities();
    expect(screen.getByText('4 communities · modularity 0.47')).toBeInTheDocument();
    expect(
      screen.getAllByRole('article').map((article) => article.querySelector('h3')?.textContent),
    ).toEqual(['Community 3', 'Community 0', 'Community 7', 'Community 12']);

    // Shares are of everyone in a community (each person belongs to exactly one).
    const largest = card('Community 3');
    expect(largest).toHaveTextContent('4,210 people · 39.2% of the network');
    expect(largest).toHaveTextContent('Mostly c# in .NET & Windows');
    expect(
      within(largest).getByRole('link', { name: 'Jon Skeet (@jon_skeet_22656)' }),
    ).toHaveAttribute('href', '/users/22656');
    expect(within(largest).getAllByRole('link')).toHaveLength(3);
    expect(largest).toHaveTextContent('and 4,207 more');
    expect(card('Community 12')).toHaveTextContent('1,502 people · 14.0% of the network');
  });

  it('marks the member’s own community', async () => {
    await openCommunities();
    expect(await within(card('Community 0')).findByText('Your community')).toBeInTheDocument();
    expect(within(card('Community 3')).queryByText('Your community')).not.toBeInTheDocument();
  });

  it('highlights a community in the graph and keeps it in the URL', async () => {
    const scrollIntoView = vi.spyOn(Element.prototype, 'scrollIntoView');
    const { user, router, graph } = await openCommunities();
    const button = highlightButton('Community 0');
    expect(button).toHaveAttribute('aria-pressed', 'false');

    await user.click(button);
    expect(router.state.location.search).toBe('?highlight=0');
    expect(button).toHaveAttribute('aria-pressed', 'true');
    expect(button).toHaveTextContent('Highlighted in graph');
    expect(legendButton(graph, 'Community 0')).toHaveAttribute('aria-pressed', 'true');
    expect(screen.getByLabelText('Highlight in graph')).toHaveValue('0');
    // The graph is brought into view and its heading focused, so keyboard users follow along.
    expect(scrollIntoView).toHaveBeenCalledWith({ behavior: 'smooth', block: 'start' });
    expect(
      within(graph).getByRole('heading', { name: 'Communities in the network' }),
    ).toHaveFocus();

    // The legend and the cards stay in sync.
    await user.click(legendButton(graph, 'Community 7'));
    expect(router.state.location.search).toBe('?highlight=7');
    expect(highlightButton('Community 7')).toHaveAttribute('aria-pressed', 'true');
    expect(highlightButton('Community 0')).toHaveAttribute('aria-pressed', 'false');

    await user.selectOptions(screen.getByLabelText('Highlight in graph'), '');
    expect(router.state.location.search).toBe('');
    expect(legendButton(graph, 'Community 7')).toHaveAttribute('aria-pressed', 'false');
  });

  it('scrolls without animation when reduced motion is preferred', async () => {
    setMediaQuery('(prefers-reduced-motion: reduce)', true);
    const scrollIntoView = vi.spyOn(Element.prototype, 'scrollIntoView');
    const { user } = await openCommunities();
    await user.click(highlightButton('Community 3'));
    expect(scrollIntoView).toHaveBeenCalledWith({ behavior: 'auto', block: 'start' });
  });

  it('restores the highlight from the URL', async () => {
    const { graph } = await openCommunities('/communities?highlight=3');
    expect(highlightButton('Community 3')).toHaveAttribute('aria-pressed', 'true');
    expect(legendButton(graph, 'Community 3')).toHaveAttribute('aria-pressed', 'true');
    expect(screen.getByLabelText('Highlight in graph')).toHaveValue('3');
    expect(screen.getByRole('option', { name: 'Community 3 (4,210 people)' })).toBeInTheDocument();
  });

  it('says when a highlighted community has nobody in the graph', async () => {
    const { user, router, graph } = await openCommunities('/communities?highlight=99');
    const warning = within(graph).getByText(/Community 99 has no members among the people shown\./);
    await user.click(within(warning).getByRole('button', { name: 'Clear highlight' }));
    expect(router.state.location.search).toBe('');
    expect(
      within(graph).queryByText(/has no members among the people shown/),
    ).not.toBeInTheDocument();
  });

  it('ignores a malformed highlight', async () => {
    await openCommunities('/communities?highlight=abc');
    expect(screen.getByLabelText('Highlight in graph')).toHaveValue('');
    expect(screen.getAllByRole('button', { name: 'Highlight in graph' })).toHaveLength(4);
  });

  it('pages through many communities', async () => {
    const many: Community[] = Array.from({ length: 14 }, (_, index) => ({
      id: 100 + index,
      size: 1_000 - index,
      dominant_category: 'Web Development',
      dominant_branch: 'javascript',
      members: [],
    }));
    server.use(
      http.get(COMMUNITIES, () =>
        HttpResponse.json({ graph_version: 41, modularity: 0.47, items: many }),
      ),
    );
    const { user } = renderApp('/communities');
    await screen.findByRole('article', { name: 'Community 100' });
    expect(screen.getAllByRole('article')).toHaveLength(12);
    const pagination = screen.getByRole('navigation', { name: 'Pagination' });
    expect(pagination).toHaveTextContent('Showing 1–12 of 14 communities');

    await user.click(within(pagination).getByRole('button', { name: 'Next' }));
    expect(
      screen.getAllByRole('article').map((article) => article.querySelector('h3')?.textContent),
    ).toEqual(['Community 112', 'Community 113']);
    // Communities without listed members show only their size.
    expect(card('Community 113')).not.toHaveTextContent('Most influential members');

    // None of the graph's communities is among these, so the legend groups them as "smaller".
    const graph = screen.getByRole('region', { name: 'Communities in the network' });
    expect(await within(graph).findByText('4 smaller communities')).toBeInTheDocument();
  });

  it('explains when there are no communities yet', async () => {
    server.use(
      http.get(COMMUNITIES, () =>
        HttpResponse.json({ graph_version: 41, modularity: 0, items: [] }),
      ),
    );
    renderApp('/communities');
    expect(await screen.findByText('No communities yet')).toBeInTheDocument();
    expect(screen.getByText('Communities appear once people are connected.')).toBeInTheDocument();
    expect(screen.queryByLabelText('Highlight in graph')).not.toBeInTheDocument();
  });

  it('shows an error with a retry', async () => {
    let fail = true;
    server.use(
      route('get', COMMUNITIES, () =>
        fail ? errorResponse(503, 'unavailable', 'Read model not bootstrapped') : undefined,
      ),
    );
    const { user } = renderApp('/communities');
    const section = await screen.findByRole('region', { name: 'All communities' });
    expect(await within(section).findByText('Service temporarily unavailable')).toBeInTheDocument();

    fail = false;
    await user.click(within(section).getByRole('button', { name: 'Try again' }));
    expect(await screen.findByRole('article', { name: 'Community 3' })).toBeInTheDocument();
  });

  it('shows skeletons while loading', async () => {
    renderApp('/communities');
    expect(await screen.findByText('Loading communities…')).toBeInTheDocument();
    await screen.findByRole('article', { name: 'Community 3' });
    await waitFor(() => {
      expect(screen.queryByText('Loading communities…')).not.toBeInTheDocument();
    });
  });

  it('has no detectable accessibility violations', async () => {
    const { container } = await openCommunities('/communities?highlight=0');
    expect(await axeViolations(container)).toEqual([]);
  });
});

describe('Community fixture', () => {
  it('is sorted by size like the API', () => {
    const sizes = communities.items.map((community) => community.size);
    expect(sizes).toEqual([...sizes].sort((a, b) => b - a));
  });
});
