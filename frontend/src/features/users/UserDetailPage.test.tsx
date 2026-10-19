import { screen, waitFor, within } from '@testing-library/react';
import { http } from 'msw';
import { describe, expect, it } from 'vitest';
import { axeViolations } from '../../test/axe';
import { jonSkeet, me, quentin } from '../../test/fixtures';
import { backend, errorResponse } from '../../test/mockBackend';
import { renderApp } from '../../test/render';
import { server } from '../../test/server';

function openProfile(id: number) {
  return renderApp(`/users/${String(id)}`);
}

async function openJonSkeet() {
  const view = openProfile(jonSkeet.id);
  await screen.findByRole('heading', { level: 1, name: 'Jon Skeet' });
  return view;
}

describe('User profile', () => {
  it('shows who the person is, with reputation first', async () => {
    await openJonSkeet();
    expect(screen.getByText('Stack Overflow user')).toBeInTheDocument();
    expect(screen.getByText('@jon_skeet_22656 · c# · .NET & Windows')).toBeInTheDocument();
    expect(screen.getByText('1,457,162')).toBeInTheDocument();
    expect(screen.getByText('100th percentile')).toBeInTheDocument();
    expect(document.title).toBe('Jon Skeet · SocialGraph');

    const link = screen.getByRole('link', { name: /View on Stack Overflow/ });
    expect(link).toHaveAttribute('href', 'https://stackoverflow.com/users/22656');
    expect(link).toHaveAttribute('target', '_blank');
    expect(link).toHaveAttribute('rel', 'noopener noreferrer');
  });

  it('shows the imported "about" text without its markup', async () => {
    await openJonSkeet();
    const about = screen.getByRole('region', { name: 'About' });
    expect(within(about).getByText('Author of C# in Depth')).toBeInTheDocument();
    expect(
      within(about).getByText('Currently a software engineer at Google, working remotely'),
    ).toBeInTheDocument();
    // The profile's http:// website is shown as text, never as a link.
    expect(within(about).getByText('http://csharpindepth.com')).toBeInTheDocument();
    expect(about.querySelector('a[href^="http:"]')).toBeNull();
    expect(within(about).getByText('Sep 2008')).toBeInTheDocument();
    expect(within(about).getByText('17,086 up · 7,966 down')).toBeInTheDocument();
  });

  it('shows the degrees of separation as a chain of people', async () => {
    await openJonSkeet();
    const card = screen.getByRole('region', { name: 'Degrees of separation' });
    const chain = await within(card).findByRole('list', { name: 'Path from you to Jon Skeet' });
    expect(card).toHaveTextContent('3 degrees of separation');
    expect(
      within(chain)
        .getAllByRole('listitem')
        .map((item) => item.textContent),
    ).toEqual(['AMYou', 'QQuentin', 'MGMarc Gravell', 'JSJon Skeet']);
    expect(
      within(chain).getByRole('link', { name: 'Marc Gravell (@marc_gravell_23354)' }),
    ).toHaveAttribute('href', '/users/23354');
  });

  it('says when there is no path yet', async () => {
    backend.state.unreachable.add(jonSkeet.id);
    await openJonSkeet();
    expect(
      await screen.findByText(/You are not connected to Jon Skeet through the network yet\./),
    ).toBeInTheDocument();
  });

  it('explains that a brand-new member’s path is still being prepared', async () => {
    server.use(http.get('*/api/v1/paths', () => errorResponse(404, 'not_found', 'Unknown user')));
    await openJonSkeet();
    expect(
      await screen.findByText('Your place in the network is still being set up'),
    ).toBeInTheDocument();
  });

  it('connects from the profile and updates the path', async () => {
    const { user } = await openJonSkeet();
    await user.click(await screen.findByRole('button', { name: 'Connect' }));
    expect(await screen.findByText('Connected with Jon Skeet')).toBeInTheDocument();
    expect(await screen.findByText('You are directly connected to Jon Skeet.')).toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'Remove connection' })).toBeInTheDocument();
    expect(backend.lastRequest('/api/v1/paths')?.query.get('min_version')).toBe('42');
  });

  it('removes a direct connection after confirmation', async () => {
    const { user } = openProfile(quentin.id);
    expect(await screen.findByText('You are directly connected to Quentin.')).toBeInTheDocument();
    await user.click(screen.getByRole('button', { name: 'Remove connection' }));
    const dialog = screen.getByRole('dialog', { name: 'Remove Quentin from your network?' });
    await user.click(within(dialog).getByRole('button', { name: 'Remove connection' }));
    expect(await screen.findByText('Removed your connection with Quentin')).toBeInTheDocument();
    expect(await screen.findByRole('button', { name: 'Connect' })).toBeInTheDocument();
  });

  it('shows Stack Overflow activity: totals, top tags, top posts and badges', async () => {
    await openJonSkeet();
    const card = screen.getByRole('region', { name: 'Stack Overflow activity' });
    const totals = await within(card).findByLabelText('Activity totals');
    expect(within(totals).getByText('Answers').nextElementSibling).toHaveTextContent('2,918');
    expect(within(totals).getByText('1,340 accepted · score 61,350')).toBeInTheDocument();
    expect(within(totals).getByText('24 gold · 482 silver · 1,008 bronze')).toBeInTheDocument();

    const tags = within(card).getByRole('region', { name: 'Top tags' });
    const csharp = within(tags).getByRole('link', { name: 'c# 697 posts' });
    expect(csharp).toHaveAttribute('href', '/topics?tag=c%23');

    const posts = within(card).getByRole('region', { name: 'Top posts' });
    const post = within(posts).getByRole('link', {
      name: /What is the difference between String and string in C#\?/,
    });
    expect(post).toHaveAttribute('href', 'https://stackoverflow.com/a/215422');
    expect(post).toHaveAttribute('rel', 'noopener noreferrer');
    // Tags given as the dataset's space-separated string are split too.
    expect(within(posts).getByRole('link', { name: 'constructor' })).toBeInTheDocument();
    expect(posts).not.toHaveTextContent('views');

    const badges = within(card).getByRole('region', { name: 'Recent badges' });
    expect(badges).toHaveTextContent('EnlightenedSilver · Dec 31, 2009');
  });

  it('shows the network metrics with ranks and the community', async () => {
    await openJonSkeet();
    const card = screen.getByRole('region', { name: 'Position in the network' });
    await within(card).findByText('PageRank');
    expect(card).toHaveTextContent('Connections');
    expect(card).toHaveTextContent('1,207');
    expect(card).toHaveTextContent('Rank #1 of 18,649');
    expect(within(card).getByRole('link', { name: 'Community 3' })).toHaveAttribute(
      'href',
      '/communities?highlight=3',
    );
  });

  it('draws the person’s network one or two steps out', async () => {
    const { user } = await openJonSkeet();
    const card = screen.getByRole('region', { name: "Jon Skeet's network" });
    expect(
      await within(card).findByRole('img', {
        name: /Jon Skeet and the 1 most influential of 1,207 direct connections, with the 1 connection among them\./,
      }),
    ).toBeInTheDocument();
    const request = backend.lastRequest('/api/v1/analytics/graph');
    expect(request?.query.get('focus')).toBe(String(jonSkeet.id));
    expect(request?.query.get('depth')).toBe('1');
    expect(request?.query.get('limit')).toBe('150');
    expect(within(card).getByText('Graph version 41')).toBeInTheDocument();

    await user.click(within(card).getByRole('radio', { name: 'Two steps away' }));
    expect(
      await within(card).findByRole('img', {
        name: /Jon Skeet, their connections and their connections’ connections: 2 people \(most influential first\) and 1 connection\./,
      }),
    ).toBeInTheDocument();
    expect(backend.lastRequest('/api/v1/analytics/graph')?.query.get('depth')).toBe('2');
  });

  it('shows the member’s own profile without a path or connect button', async () => {
    renderApp(`/users/${String(me.id)}`);
    expect(
      await screen.findByRole('heading', { level: 1, name: 'Ada Member' }),
    ).toBeInTheDocument();
    expect(screen.getByText('You')).toBeInTheDocument();
    expect(screen.getByText('SocialGraph member')).toBeInTheDocument();
    expect(screen.queryByRole('region', { name: 'Degrees of separation' })).not.toBeInTheDocument();
    expect(screen.queryByRole('button', { name: 'Connect' })).not.toBeInTheDocument();
    expect(screen.queryByRole('link', { name: /View on Stack Overflow/ })).not.toBeInTheDocument();
    expect(await screen.findByText('No Stack Overflow activity')).toBeInTheDocument();
    expect(
      screen.getByText(
        'Ada Member joined SocialGraph directly, so there are no imported posts or badges.',
      ),
    ).toBeInTheDocument();
    expect(screen.getByRole('region', { name: 'Your network graph' })).toBeInTheDocument();
  });

  it('shows "Person not found" for unknown and malformed ids', async () => {
    renderApp('/users/999999');
    expect(
      await screen.findByRole('heading', { level: 1, name: 'Person not found' }),
    ).toBeInTheDocument();
    expect(screen.getByText('There is no one with this id in the network.')).toBeInTheDocument();
  });

  it('rejects malformed profile addresses without a request', async () => {
    renderApp('/users/12abc');
    expect(
      await screen.findByRole('heading', { level: 1, name: 'Person not found' }),
    ).toBeInTheDocument();
    expect(screen.getByText('That profile address is not valid.')).toBeInTheDocument();
    expect(backend.requests('/api/v1/users/12abc')).toHaveLength(0);
  });

  it('shows errors of each section separately', async () => {
    server.use(
      http.get('*/api/v1/users/:id/activity', () =>
        errorResponse(503, 'unavailable', 'Content service down'),
      ),
      http.get('*/api/v1/users/:id/metrics', () => errorResponse(500, 'internal', 'boom')),
    );
    await openJonSkeet();
    expect(await screen.findByText('Activity is unavailable')).toBeInTheDocument();
    expect(await screen.findByText('Something went wrong on our side')).toBeInTheDocument();
    // The rest of the page still works.
    expect(screen.getByRole('region', { name: 'About' })).toBeInTheDocument();
  });

  it('offers a retry when the profile cannot be loaded', async () => {
    let fail = true;
    server.use(
      http.get('*/api/v1/users/:id', () =>
        fail ? errorResponse(500, 'internal', 'boom') : undefined,
      ),
    );
    const { user } = openProfile(jonSkeet.id);
    expect(await screen.findByText("We couldn't load this profile")).toBeInTheDocument();
    fail = false;
    await user.click(screen.getByRole('button', { name: 'Try again' }));
    expect(await screen.findByRole('heading', { level: 1, name: 'Jon Skeet' })).toBeInTheDocument();
  });

  it('has no detectable accessibility violations', async () => {
    const { container } = await openJonSkeet();
    await screen.findByLabelText('Activity totals');
    await screen.findByRole('list', { name: 'Path from you to Jon Skeet' });
    await waitFor(() => {
      expect(screen.getByRole('region', { name: 'Position in the network' })).toHaveTextContent(
        'PageRank',
      );
    });
    expect(await axeViolations(container)).toEqual([]);
  });
});
