import { screen, within } from '@testing-library/react';
import { http, HttpResponse } from 'msw';
import type { RouteObject } from 'react-router';
import { describe, expect, it, vi } from 'vitest';
import { RouteErrorPage } from '../features/errors/RouteErrorPage';
import { axeViolations } from '../test/axe';
import { me } from '../test/fixtures';
import { renderApp } from '../test/render';
import { allowRenderErrors } from '../test/renderErrors';
import { server } from '../test/server';

function Crash({ error }: { error: unknown }): never {
  throw error;
}

/** A one-page app whose page throws `error` while rendering. */
function crashingRoutes(error: unknown): RouteObject[] {
  return [{ path: '/', element: <Crash error={error} />, errorElement: <RouteErrorPage /> }];
}

/** The alert around an error page's heading (the toast regions are alerts too). */
function errorAlert(heading: HTMLElement): HTMLElement {
  const alert = heading.closest<HTMLElement>('[role="alert"]');
  if (!alert) throw new Error('The error page is not announced as an alert');
  return alert;
}

describe('Navigation shell', () => {
  it('marks the current page and moves focus to the new page after navigating', async () => {
    const scrollTo = vi.spyOn(window, 'scrollTo');
    const { user, router } = renderApp('/');
    await screen.findByRole('heading', { level: 1, name: 'Network overview' });
    const nav = screen.getByRole('navigation', { name: 'Main' });
    expect(
      within(nav)
        .getAllByRole('link')
        .map((link) => link.textContent),
    ).toEqual(['Overview', 'My network', 'Communities', 'People', 'Topics']);
    expect(within(nav).getByRole('link', { name: 'Overview' })).toHaveAttribute(
      'aria-current',
      'page',
    );

    await user.click(within(nav).getByRole('link', { name: 'People' }));
    expect(await screen.findByRole('heading', { level: 1, name: 'People' })).toBeInTheDocument();
    expect(router.state.location.pathname).toBe('/people');
    expect(within(nav).getByRole('link', { name: 'People' })).toHaveAttribute(
      'aria-current',
      'page',
    );
    // "Overview" matches only its own path, not every page below "/".
    expect(within(nav).getByRole('link', { name: 'Overview' })).not.toHaveAttribute('aria-current');
    expect(screen.getByRole('main')).toHaveFocus();
    expect(scrollTo).toHaveBeenCalledWith({ top: 0 });
  });

  it('offers a skip link and a link to the member’s own profile', async () => {
    renderApp('/');
    await screen.findByRole('heading', { level: 1, name: 'Network overview' });
    expect(screen.getByRole('link', { name: 'Skip to main content' })).toHaveAttribute(
      'href',
      '#main',
    );
    expect(screen.getByRole('main')).toHaveAttribute('id', 'main');
    expect(screen.getByRole('link', { name: `${me.name} @${me.handle}` })).toHaveAttribute(
      'href',
      `/users/${String(me.id)}`,
    );
    expect(screen.getByRole('link', { name: 'SocialGraph home' })).toHaveAttribute('href', '/');
  });
});

describe('Not found', () => {
  it('shows "Page not found" inside the app for unknown addresses', async () => {
    const { user } = renderApp('/no-such-page');
    expect(
      await screen.findByRole('heading', { level: 1, name: 'Page not found' }),
    ).toBeInTheDocument();
    expect(
      screen.getByText('The page you are looking for does not exist or has moved.'),
    ).toBeInTheDocument();
    expect(document.title).toBe('Page not found · SocialGraph');
    // The navigation stays available.
    expect(screen.getByRole('navigation', { name: 'Main' })).toBeInTheDocument();
    expect(screen.getByRole('link', { name: 'Go to the overview' })).toHaveAttribute('href', '/');

    await user.click(screen.getByRole('link', { name: 'Find people' }));
    expect(await screen.findByRole('heading', { level: 1, name: 'People' })).toBeInTheDocument();
  });

  it('asks anonymous visitors to sign in first', async () => {
    const { router } = renderApp('/no-such-page', { signedIn: false });
    expect(await screen.findByRole('heading', { level: 1, name: 'Sign in' })).toBeInTheDocument();
    expect(router.state.location.pathname).toBe('/sign-in');
  });

  it('handles addresses outside every route', async () => {
    const routes: RouteObject[] = [
      { path: '/', element: <p>Home</p>, errorElement: <RouteErrorPage /> },
    ];
    renderApp('/missing', { routes });
    expect(
      await screen.findByRole('heading', { level: 1, name: 'Page not found' }),
    ).toBeInTheDocument();
  });

  it('has no detectable accessibility violations', async () => {
    const { container } = renderApp('/no-such-page');
    await screen.findByRole('heading', { level: 1, name: 'Page not found' });
    expect(await axeViolations(container)).toEqual([]);
  });
});

describe('Unexpected errors', () => {
  it('shows a recoverable error page instead of a blank screen', async () => {
    allowRenderErrors();
    renderApp('/', { routes: crashingRoutes(new Error('Cannot read properties of undefined')) });
    const alert = errorAlert(
      await screen.findByRole('heading', { level: 1, name: 'Something went wrong' }),
    );
    expect(alert).toHaveTextContent('Reloading usually fixes it.');
    expect(within(alert).getByRole('button', { name: 'Reload' })).toBeEnabled();
    expect(within(alert).getByRole('link', { name: 'Go to the overview' })).toHaveAttribute(
      'href',
      '/',
    );
    // Outside the shell the error page is the main landmark itself.
    expect(screen.getByRole('main')).toContainElement(alert);
    expect(document.title).toBe('Something went wrong · SocialGraph');
    // The technical message is not shown to the member.
    expect(screen.queryByText(/Cannot read properties/)).not.toBeInTheDocument();
  });

  it('asks for a reload when a page of an older deployment is gone', async () => {
    allowRenderErrors();
    const error = new TypeError(
      'Failed to fetch dynamically imported module: https://socialgraph.example/assets/PeoplePage-4f2a9c.js',
    );
    renderApp('/', { routes: crashingRoutes(error) });
    expect(
      await screen.findByRole('heading', { level: 1, name: 'A new version is available' }),
    ).toBeInTheDocument();
    expect(
      screen.getByText('SocialGraph was updated while this page was open. Reload to continue.'),
    ).toBeInTheDocument();
    expect(document.title).toBe('Update available · SocialGraph');
  });

  it('keeps the navigation usable when a page crashes', async () => {
    allowRenderErrors();
    // A response that breaks the contract (no `items`) makes the People page throw while rendering.
    server.use(
      http.get('*/api/v1/users', () => HttpResponse.json({ total: 1, limit: 20, offset: 0 })),
    );
    const { user } = renderApp('/people');
    const alert = errorAlert(
      await screen.findByRole('heading', { level: 1, name: 'Something went wrong' }),
    );
    // Rendered inside the shell's <main>, next to the navigation.
    expect(screen.getByRole('main')).toContainElement(alert);
    expect(screen.getAllByRole('main')).toHaveLength(1);

    await user.click(
      within(screen.getByRole('navigation', { name: 'Main' })).getByRole('link', {
        name: 'Overview',
      }),
    );
    expect(
      await screen.findByRole('heading', { level: 1, name: 'Network overview' }),
    ).toBeInTheDocument();
    expect(screen.queryByRole('heading', { name: 'Something went wrong' })).not.toBeInTheDocument();
  });
});
