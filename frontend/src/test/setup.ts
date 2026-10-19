import '@testing-library/jest-dom/vitest';
import { cleanup, configure } from '@testing-library/react';
import { afterAll, afterEach, beforeAll, beforeEach, expect } from 'vitest';
import { resetApiClient } from '../api/client';
import { graphVersionStore } from '../api/graphVersion';
import { provisioningRetry } from '../app/queryClient';
import { wireApiClient } from '../app/wireApiClient';
import { sessionStore } from '../auth/sessionStore';
import { profilePolling } from '../features/profile/useMyProfile';
import { installDomShims, resetMediaQueries } from './domShims';
import { backend } from './mockBackend';
import { clearQueryClients } from './render';
import { server } from './server';

// Lazy page chunks are compiled on first use, which can take a moment in the first test.
configure({ asyncUtilTimeout: 5000 });
installDomShims();

beforeAll(() => {
  server.listen({ onUnhandledFrame: 'error' });
});

beforeEach(() => {
  window.localStorage.clear();
  sessionStore.resetForTests();
  graphVersionStore.reset();
  resetApiClient();
  wireApiClient();
  backend.reset();
  resetMediaQueries();
  // Production waits are seconds long; tests poll and retry quickly.
  profilePolling.intervalMs = 20;
  profilePolling.attempts = 20;
  provisioningRetry.attempts = 1;
  provisioningRetry.delayMs = 10;
});

afterEach(() => {
  cleanup();
  clearQueryClients();
  server.resetHandlers();
  expect(backend.state.unhandled, 'requests without a mock handler').toEqual([]);
});

afterAll(() => {
  server.close();
});
