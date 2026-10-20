import { screen, waitFor, within } from '@testing-library/react';
import { HttpResponse } from 'msw';
import { describe, expect, it } from 'vitest';
import { axeViolations } from '../../test/axe';
import { people } from '../../test/fixtures';
import { backend, errorResponse, route } from '../../test/mockBackend';
import { renderApp } from '../../test/render';
import { server } from '../../test/server';
import { PEOPLE_PAGE_SIZE } from './PeoplePage';

const USERS = '/api/v1/users';

async function openPeople(path = '/people') {
  const view = renderApp(path);
  await screen.findByRole('heading', { level: 1, name: 'People' });
  const table = await screen.findByRole('table', { name: /People, sorted by/ });
  return { ...view, table };
}

function rows(table: HTMLElement) {
  return within(table).getAllByRole('row').slice(1);
}

function searches(): (string | null)[] {
  return backend.requests(USERS).map((request) => request.query.get('q'));
}

describe('People', () => {
  it('lists people by reputation with their technology, location and percentile', async () => {
    const { table } = await openPeople();
    const [first, ...rest] = rows(table);
    expect(first).toHaveTextContent('Jon Skeet');
    expect(first).toHaveTextContent('c#.NET & Windows');
    expect(first).toHaveTextContent('Reading, United Kingdom');
    expect(first).toHaveTextContent('1,457,162100th percentile');
    expect(rest.at(-1)).toHaveTextContent('You');
    expect(screen.getByText('9 people found')).toBeInTheDocument();
    expect(backend.lastRequest(USERS)?.query.get('limit')).toBe(String(PEOPLE_PAGE_SIZE));
  });

  it('searches after a pause in typing, with a single request', async () => {
    const { user, router } = await openPeople();
    await user.type(screen.getByRole('searchbox', { name: 'Search' }), 'skeet');

    const table = await screen.findByRole('table', { name: /People, sorted by/ });
    await waitFor(() => {
      expect(rows(table)).toHaveLength(1);
    });
    expect(rows(table)[0]).toHaveTextContent('Jon Skeet');
    expect(searches().filter((q) => q !== null)).toEqual(['skeet']);
    expect(router.state.location.search).toBe('?q=skeet');
    expect(screen.getByText('1 person found')).toBeInTheDocument();
  });

  it('searches immediately on Enter', async () => {
    const { user } = await openPeople();
    await user.type(screen.getByRole('searchbox', { name: 'Search' }), 'gravell{Enter}');
    await waitFor(() => {
      expect(searches()).toContain('gravell');
    });
    expect(searches().filter((q) => q !== null)).toEqual(['gravell']);
  });

  it('pages through results and keeps the page in the URL', async () => {
    server.use(
      route('get', `*${USERS}`, ({ url }) => {
        const offset = Number(url.searchParams.get('offset') ?? '0');
        const items = offset === 0 ? people.slice(0, 4) : people.slice(4);
        return HttpResponse.json({ items, total: 45, limit: PEOPLE_PAGE_SIZE, offset });
      }),
    );
    const { user, router, table } = await openPeople();
    const pagination = screen.getByRole('navigation', { name: 'Pagination' });
    expect(pagination).toHaveTextContent('Showing 1-20 of 45 people');
    expect(within(pagination).getByRole('button', { name: 'Previous' })).toBeDisabled();

    await user.click(within(pagination).getByRole('button', { name: 'Next' }));
    await waitFor(() => {
      expect(screen.getByRole('navigation', { name: 'Pagination' })).toHaveTextContent(
        'Page 2 of 3',
      );
    });
    expect(backend.lastRequest(USERS)?.query.get('offset')).toBe('20');
    expect(router.state.location.search).toBe('?page=2');
    await waitFor(() => {
      expect(rows(table)[0]).toHaveTextContent('Hans Passant');
    });

    // Page 1 comes back from the cache.
    await user.click(screen.getByRole('button', { name: 'Previous' }));
    await waitFor(() => {
      expect(rows(table)[0]).toHaveTextContent('Jon Skeet');
    });
    expect(router.state.location.search).toBe('');
  });

  it('restores the search and page from the URL', async () => {
    renderApp('/people?q=jon&page=2&sort=name');
    await screen.findByRole('heading', { level: 1, name: 'People' });
    expect(screen.getByRole('searchbox', { name: 'Search' })).toHaveValue('jon');
    await waitFor(() => {
      expect(backend.lastRequest(USERS)?.query.get('offset')).toBe('20');
    });
    expect(backend.lastRequest(USERS)?.query.get('sort')).toBe('name');
    expect(screen.getByLabelText('Sort by')).toHaveValue('name');
  });

  it('filters by category, technology and reputation', async () => {
    const { user, router } = await openPeople();
    const category = screen.getByLabelText('Category');
    const technology = screen.getByLabelText('Technology');
    await waitFor(() => {
      expect(category).toBeEnabled();
    });

    await user.selectOptions(category, 'Web Development');
    await waitFor(() => {
      expect(backend.lastRequest(USERS)?.query.get('category')).toBe('Web Development');
    });
    expect(
      within(technology)
        .getAllByRole('option')
        .map((option) => option.textContent),
    ).toEqual(['All technologies', 'javascript', 'php', 'css']);

    // Picking a technology from another category switches the category with it.
    await user.selectOptions(category, '');
    await user.selectOptions(technology, 'c#');
    await waitFor(() => {
      expect(backend.lastRequest(USERS)?.query.get('branch')).toBe('c#');
    });
    expect(backend.lastRequest(USERS)?.query.get('category')).toBe('.NET & Windows');
    expect(category).toHaveValue('.NET & Windows');

    await user.selectOptions(screen.getByLabelText('Reputation'), '50000');
    await waitFor(() => {
      expect(backend.lastRequest(USERS)?.query.get('min_reputation')).toBe('50000');
    });
    const table = screen.getByRole('table', { name: /People, sorted by/ });
    await waitFor(() => {
      expect(rows(table).map((row) => row.querySelector('a')?.textContent)).toEqual([
        'JSJon Skeet @jon_skeet_22656',
        'MGMarc Gravell @marc_gravell_23354',
        'HPHans Passant @hans_passant_17034',
      ]);
    });

    await user.selectOptions(screen.getByLabelText('Sort by'), 'name');
    await waitFor(() => {
      expect(router.state.location.search).toContain('sort=name');
    });

    await user.click(screen.getByRole('button', { name: 'Clear filters' }));
    await waitFor(() => {
      expect(router.state.location.search).toBe('?sort=name');
    });
    expect(category).toHaveValue('');
  });

  it('suggests clearing filters when nothing matches', async () => {
    const { user } = await openPeople();
    await user.type(screen.getByRole('searchbox', { name: 'Search' }), 'zzzz{Enter}');
    expect(await screen.findByText('No people match these filters')).toBeInTheDocument();
    expect(screen.getByText('0 people found')).toBeInTheDocument();
    const [, clear] = screen.getAllByRole('button', { name: 'Clear filters' });
    await user.click(clear!);
    expect(await screen.findByRole('table', { name: /People, sorted by/ })).toBeInTheDocument();
    expect(screen.getByRole('searchbox', { name: 'Search' })).toHaveValue('');
  });

  it('shows an error with a retry', async () => {
    let fail = true;
    server.use(
      route('get', `*${USERS}`, () =>
        fail ? errorResponse(500, 'internal', 'Search index unavailable') : undefined,
      ),
    );
    const { user } = renderApp('/people');
    expect(await screen.findByText('Something went wrong on our side')).toBeInTheDocument();
    fail = false;
    await user.click(screen.getByRole('button', { name: 'Try again' }));
    expect(await screen.findByRole('table', { name: /People, sorted by/ })).toBeInTheDocument();
  });

  it('has no detectable accessibility violations', async () => {
    const { container } = await openPeople();
    expect(await axeViolations(container)).toEqual([]);
  });
});
