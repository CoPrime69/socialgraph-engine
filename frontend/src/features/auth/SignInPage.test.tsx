import { screen, waitFor, within } from '@testing-library/react';
import { http, HttpResponse } from 'msw';
import { describe, expect, it } from 'vitest';
import { sessionStore } from '../../auth/sessionStore';
import { axeViolations } from '../../test/axe';
import { backend, errorResponse, PASSWORD } from '../../test/mockBackend';
import { renderApp } from '../../test/render';
import { server } from '../../test/server';

const LOGIN = '*/api/v1/auth/login';

async function openSignIn(path = '/sign-in') {
  const view = renderApp(path, { signedIn: false });
  await screen.findByRole('heading', { level: 1, name: 'Sign in' });
  return view;
}

describe('Sign in', () => {
  it('validates the form before sending it', async () => {
    const { user } = await openSignIn();
    await user.click(screen.getByRole('button', { name: 'Sign in' }));

    const handle = screen.getByLabelText('Handle');
    expect(handle).toHaveAccessibleDescription('Enter your handle.');
    expect(handle).toHaveAttribute('aria-invalid', 'true');
    expect(handle).toHaveFocus();
    expect(screen.getByLabelText('Password')).toHaveAccessibleDescription('Enter your password.');
    expect(backend.requests('/api/v1/auth/login')).toHaveLength(0);
  });

  it('signs in with a normalised handle and opens the overview', async () => {
    const { user, router } = await openSignIn();
    await user.type(screen.getByLabelText('Handle'), '  ADA_Member ');
    await user.type(screen.getByLabelText('Password'), PASSWORD);
    await user.click(screen.getByRole('button', { name: 'Sign in' }));

    expect(
      await screen.findByRole('heading', { level: 1, name: 'Network overview' }),
    ).toBeInTheDocument();
    expect(router.state.location.pathname).toBe('/');
    expect(backend.lastRequest('/api/v1/auth/login')?.body).toEqual({
      handle: 'ada_member',
      password: PASSWORD,
    });
    expect(sessionStore.getToken()).toBe('test-token');
  });

  it('returns to the page that required signing in', async () => {
    const { user, router } = await openSignIn('/people?q=jon');
    expect(router.state.location.pathname).toBe('/sign-in');
    await user.type(screen.getByLabelText('Handle'), 'ada_member');
    await user.type(screen.getByLabelText('Password'), PASSWORD);
    await user.click(screen.getByRole('button', { name: 'Sign in' }));

    expect(await screen.findByRole('heading', { level: 1, name: 'People' })).toBeInTheDocument();
    expect(router.state.location.pathname).toBe('/people');
    expect(router.state.location.search).toBe('?q=jon');
  });

  it('explains a wrong password, clears it and focuses it', async () => {
    const { user } = await openSignIn();
    await user.type(screen.getByLabelText('Handle'), 'ada_member');
    await user.type(screen.getByLabelText('Password'), 'not-the-password');
    await user.click(screen.getByRole('button', { name: 'Sign in' }));

    expect(await screen.findByText('Incorrect handle or password.')).toBeInTheDocument();
    const password = screen.getByLabelText('Password');
    expect(password).toHaveValue('');
    expect(password).toHaveFocus();
    expect(sessionStore.getSnapshot()).toBeNull();
  });

  it('shows server validation errors next to the fields', async () => {
    server.use(
      http.post(LOGIN, () =>
        errorResponse(422, 'validation_failed', 'Invalid input', {
          details: [
            { field: 'handle', message: 'must be 3-32 characters' },
            { field: 'captcha', message: 'is required' },
          ],
        }),
      ),
    );
    const { user } = await openSignIn();
    await user.type(screen.getByLabelText('Handle'), 'ada_member');
    await user.type(screen.getByLabelText('Password'), PASSWORD);
    await user.click(screen.getByRole('button', { name: 'Sign in' }));

    const handle = screen.getByLabelText('Handle');
    await waitFor(() => {
      expect(handle).toHaveAccessibleDescription('Must be 3-32 characters.');
    });
    expect(handle).toHaveFocus();
    expect(screen.getByText('captcha: is required')).toBeInTheDocument();
  });

  it('waits out a rate limit before allowing another attempt', async () => {
    server.use(
      http.post(LOGIN, () =>
        errorResponse(429, 'rate_limited', 'Too many requests', {
          headers: { 'Retry-After': '2' },
        }),
      ),
    );
    const { user } = await openSignIn();
    await user.type(screen.getByLabelText('Handle'), 'ada_member');
    await user.type(screen.getByLabelText('Password'), PASSWORD);
    await user.click(screen.getByRole('button', { name: 'Sign in' }));

    expect(
      await screen.findByText('Please wait 2 seconds before trying again.'),
    ).toBeInTheDocument();
    expect(screen.getByRole('button', { name: /Try again in \ds/ })).toBeDisabled();
    expect(await screen.findByRole('button', { name: 'Sign in' }, { timeout: 4000 })).toBeEnabled();
  });

  it('handles the gateway’s HTML 429 page without Retry-After', async () => {
    server.use(
      http.post(
        LOGIN,
        () =>
          new HttpResponse('<html><body>429 Too Many Requests</body></html>', {
            status: 429,
            headers: { 'Content-Type': 'text/html' },
          }),
      ),
    );
    const { user } = await openSignIn();
    await user.type(screen.getByLabelText('Handle'), 'ada_member');
    await user.type(screen.getByLabelText('Password'), PASSWORD);
    await user.click(screen.getByRole('button', { name: 'Sign in' }));

    expect(
      await screen.findByText('Please wait a moment before trying again.'),
    ).toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'Sign in' })).toBeEnabled();
  });

  it('explains when the service is unreachable', async () => {
    server.use(http.post(LOGIN, () => HttpResponse.error()));
    const { user } = await openSignIn();
    await user.type(screen.getByLabelText('Handle'), 'ada_member');
    await user.type(screen.getByLabelText('Password'), PASSWORD);
    await user.click(screen.getByRole('button', { name: 'Sign in' }));

    expect(
      await screen.findByText("Can't reach the server. Check your connection and try again."),
    ).toBeInTheDocument();
  });

  it('can show the password', async () => {
    const { user } = await openSignIn();
    const password = screen.getByLabelText('Password');
    expect(password).toHaveAttribute('type', 'password');
    await user.click(screen.getByRole('button', { name: 'Show password' }));
    expect(password).toHaveAttribute('type', 'text');
    expect(screen.getByRole('button', { name: 'Hide password' })).toHaveAttribute(
      'aria-pressed',
      'true',
    );
  });

  it('sends signed-in visitors to the overview', async () => {
    const { router } = renderApp('/sign-in');
    expect(
      await screen.findByRole('heading', { level: 1, name: 'Network overview' }),
    ).toBeInTheDocument();
    expect(router.state.location.pathname).toBe('/');
  });

  it('has no detectable accessibility violations', async () => {
    const { container } = await openSignIn();
    expect(within(container).getByRole('link', { name: 'Create an account' })).toHaveAttribute(
      'href',
      '/register',
    );
    expect(await axeViolations(container)).toEqual([]);
  });
});
