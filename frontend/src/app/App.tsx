import { useState } from 'react';
// The core RouterProvider: the `react-router/dom` variant only adds `flushSync` navigations, which
// the app does not use, and a single entry point guarantees a single copy of the router contexts
// in every module system (the Node test runner loads `react-router/dom` as a separate copy).
import { createBrowserRouter, RouterProvider } from 'react-router';
import { AppProviders } from './AppProviders';
import { createQueryClient } from './queryClient';
import { appRoutes } from './routes';

const router = createBrowserRouter(appRoutes);

export function App() {
  const [queryClient] = useState(createQueryClient);
  return (
    <AppProviders queryClient={queryClient}>
      <RouterProvider router={router} />
    </AppProviders>
  );
}
