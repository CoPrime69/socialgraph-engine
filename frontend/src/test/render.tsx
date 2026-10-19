import type { QueryClient } from '@tanstack/react-query';
import { render, type RenderResult } from '@testing-library/react';
import userEvent, { type UserEvent } from '@testing-library/user-event';
import { createMemoryRouter, RouterProvider, type RouteObject } from 'react-router';
import { AppProviders } from '../app/AppProviders';
import { createQueryClient } from '../app/queryClient';
import { appRoutes } from '../app/routes';
import { sessionStore } from '../auth/sessionStore';
import { authResponse } from './fixtures';

const clients = new Set<QueryClient>();

/** The production query client, with instant retries so failing tests do not wait. */
export function createTestQueryClient(): QueryClient {
  const client = createQueryClient();
  const defaults = client.getDefaultOptions();
  client.setDefaultOptions({ ...defaults, queries: { ...defaults.queries, retryDelay: 1 } });
  clients.add(client);
  return client;
}

/** Drops every cache created by a test (called after each test). */
export function clearQueryClients(): void {
  for (const client of clients) client.clear();
  clients.clear();
}

export interface AppRenderResult extends RenderResult {
  user: UserEvent;
  router: ReturnType<typeof createMemoryRouter>;
  queryClient: QueryClient;
}

interface RenderAppOptions {
  /** Start with the fixture member signed in (default true). */
  signedIn?: boolean;
  routes?: RouteObject[];
}

/** Renders the whole app (routes, providers, shell) at `path`, like a page load. */
export function renderApp(
  path = '/',
  { signedIn = true, routes = appRoutes }: RenderAppOptions = {},
): AppRenderResult {
  if (signedIn) sessionStore.start(authResponse);
  const queryClient = createTestQueryClient();
  const router = createMemoryRouter(routes, { initialEntries: [path] });
  const user = userEvent.setup();
  const view = render(
    <AppProviders queryClient={queryClient}>
      <RouterProvider router={router} />
    </AppProviders>,
  );
  return { ...view, user, router, queryClient };
}

/** A promise with its resolve function exposed, to hold a mocked response until released. */
export function deferred(): { promise: Promise<void>; resolve: () => void } {
  let resolve: () => void = () => undefined;
  const promise = new Promise<void>((done) => {
    resolve = done;
  });
  return { promise, resolve };
}
