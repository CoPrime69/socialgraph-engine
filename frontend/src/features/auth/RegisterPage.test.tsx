import { screen, waitFor, within } from '@testing-library/react';
import { http, HttpResponse } from 'msw';
import { describe, expect, it } from 'vitest';
import { sessionStore } from '../../auth/sessionStore';
import { axeViolations } from '../../test/axe';
import { taxonomy } from '../../test/fixtures';
import { backend, errorResponse, route } from '../../test/mockBackend';
import { renderApp } from '../../test/render';
import { server } from '../../test/server';

const REGISTER = '*/api/v1/auth/register';

async function openRegister() {
  const view = renderApp('/register', { signedIn: false });
  await screen.findByRole('heading', { level: 1, name: 'Create your account' });
  await waitFor(() => {
    expect(screen.getByLabelText('Category')).toBeEnabled();
  });
  return view;
}

async function fillValidForm(user: Awaited<ReturnType<typeof openRegister>>['user']) {
  await user.type(screen.getByLabelText('Handle'), 'grace_member');
  await user.type(screen.getByLabelText('Display name'), 'Grace Member');
  await user.type(screen.getByLabelText('Password'), 'a-long-password');
  await user.selectOptions(screen.getByLabelText('Category'), 'Systems Programming');
  await user.selectOptions(screen.getByLabelText('Primary technology'), 'c++');
}

describe('Registration', () => {
  it('groups technologies by category and filters them once a category is chosen', async () => {
    const { user } = await openRegister();
    const branch = screen.getByLabelText('Primary technology');
    const groups = within(branch).getAllByRole('group');
    expect(groups.map((group) => group.getAttribute('label'))).toEqual(taxonomy.categories);

    await user.selectOptions(screen.getByLabelText('Category'), 'Web Development');
    expect(within(branch).queryAllByRole('group')).toHaveLength(0);
    expect(
      within(branch)
        .getAllByRole('option')
        .map((option) => option.textContent),
    ).toEqual(['Choose a technology', 'javascript', 'php', 'css']);
  });

  it('selects the category of the technology chosen first', async () => {
    const { user } = await openRegister();
    await user.selectOptions(screen.getByLabelText('Primary technology'), 'python');
    expect(screen.getByLabelText('Category')).toHaveValue('Scripting Languages');

    // Changing to another category clears a technology that does not belong to it.
    await user.selectOptions(screen.getByLabelText('Category'), 'Web Development');
    expect(screen.getByLabelText('Primary technology')).toHaveValue('');
  });

  it('validates every field and focuses the first problem', async () => {
    const { user } = await openRegister();
    await user.type(screen.getByLabelText('Handle'), 'Grace-Hopper');
    await user.type(screen.getByLabelText('Password'), 'short');
    await user.click(screen.getByRole('button', { name: 'Create account' }));

    const handle = screen.getByLabelText('Handle');
    expect(handle).toHaveValue('grace-hopper');
    expect(handle).toHaveAccessibleDescription(
      expect.stringContaining('Use 3-32 characters: lowercase letters, numbers or underscores.'),
    );
    expect(handle).toHaveFocus();
    expect(screen.getByLabelText('Display name')).toHaveAccessibleDescription('Enter your name.');
    expect(screen.getByLabelText('Password')).toHaveAccessibleDescription(
      expect.stringContaining('Use at least 8 characters.'),
    );
    expect(screen.getByLabelText('Category')).toHaveAccessibleDescription('Choose a category.');
    expect(backend.requests('/api/v1/auth/register')).toHaveLength(0);
  });

  it('counts the characters of the optional "about" text', async () => {
    const { user } = await openRegister();
    const about = screen.getByLabelText(/About you/);
    await user.type(about, 'Systems programmer.');
    expect(screen.getByText('19/500')).toBeInTheDocument();
  });

  it('creates the account, waits for the profile and opens the overview', async () => {
    const { user, router } = await openRegister();
    await fillValidForm(user);
    await user.type(screen.getByLabelText(/Location/), '  Arlington ');
    await user.click(screen.getByRole('button', { name: 'Create account' }));

    expect(await screen.findByText('Setting up your profile…')).toBeInTheDocument();
    expect(
      await screen.findByRole('heading', { level: 1, name: 'Network overview' }),
    ).toBeInTheDocument();
    expect(router.state.location.pathname).toBe('/');
    expect(screen.getByRole('link', { name: /Grace Member/ })).toHaveAttribute(
      'href',
      `/users/${String(sessionStore.getSnapshot()?.user.id)}`,
    );
    expect(backend.lastRequest('/api/v1/auth/register', 'POST')?.body).toEqual({
      handle: 'grace_member',
      name: 'Grace Member',
      password: 'a-long-password',
      category: 'Systems Programming',
      branch: 'c++',
      location: 'Arlington',
    });
    // Polled while graph-service created the profile (404 twice, then 200).
    expect(backend.requests('/api/v1/me/profile').length).toBeGreaterThanOrEqual(3);
  });

  it('asks for another handle when it is taken', async () => {
    const { user } = await openRegister();
    await user.type(screen.getByLabelText('Handle'), 'ada_member');
    await user.type(screen.getByLabelText('Display name'), 'Another Ada');
    await user.type(screen.getByLabelText('Password'), 'a-long-password');
    await user.selectOptions(screen.getByLabelText('Primary technology'), 'javascript');
    await user.click(screen.getByRole('button', { name: 'Create account' }));

    const handle = screen.getByLabelText('Handle');
    await waitFor(() => {
      expect(handle).toHaveAccessibleDescription(
        expect.stringContaining('That handle is already taken. Try another one.'),
      );
    });
    expect(handle).toHaveFocus();
    expect(sessionStore.getSnapshot()).toBeNull();
  });

  it('maps server validation errors onto the form', async () => {
    server.use(
      http.post(REGISTER, () =>
        errorResponse(422, 'validation_failed', 'Invalid input', {
          details: [
            { field: 'branch', message: 'unknown branch' },
            { field: 'influence', message: 'is not accepted' },
          ],
        }),
      ),
    );
    const { user } = await openRegister();
    await fillValidForm(user);
    await user.click(screen.getByRole('button', { name: 'Create account' }));

    expect(
      await screen.findByText('Please correct the following: influence: is not accepted'),
    ).toBeInTheDocument();
    const branch = screen.getByLabelText('Primary technology');
    expect(branch).toHaveAccessibleDescription(expect.stringContaining('Unknown branch.'));
    expect(branch).toHaveFocus();
  });

  it('shows the rate limit and blocks retries meanwhile', async () => {
    server.use(
      http.post(REGISTER, () =>
        errorResponse(429, 'rate_limited', 'Too many requests', {
          headers: { 'Retry-After': '30' },
        }),
      ),
    );
    const { user } = await openRegister();
    await fillValidForm(user);
    await user.click(screen.getByRole('button', { name: 'Create account' }));

    expect(
      await screen.findByText('Please wait 30 seconds before trying again.'),
    ).toBeInTheDocument();
    expect(screen.getByRole('button', { name: /Try again in \d+s/ })).toBeDisabled();
  });

  it('reports unexpected failures', async () => {
    server.use(http.post(REGISTER, () => new HttpResponse('upstream error', { status: 502 })));
    const { user } = await openRegister();
    await fillValidForm(user);
    await user.click(screen.getByRole('button', { name: 'Create account' }));
    expect(
      await screen.findByText(
        'Service temporarily unavailable. Part of the platform is starting up or busy. Please try again in a moment.',
      ),
    ).toBeInTheDocument();
  });

  it('offers a retry when the categories cannot be loaded', async () => {
    let calls = 0;
    server.use(
      route(
        'get',
        '*/api/v1/meta/taxonomy',
        () => {
          calls += 1;
          return calls === 1 ? errorResponse(500, 'internal', 'Database unavailable') : undefined;
        },
        { auth: false },
      ),
    );
    const { user } = renderApp('/register', { signedIn: false });
    expect(await screen.findByText("Couldn't load categories")).toBeInTheDocument();
    await user.click(screen.getByRole('button', { name: 'Try again' }));
    await waitFor(() => {
      expect(screen.getByLabelText('Category')).toBeEnabled();
    });
  });

  it('has no detectable accessibility violations', async () => {
    const { container } = await openRegister();
    expect(await axeViolations(container)).toEqual([]);
  });
});
