import { defineConfig, devices } from '@playwright/test';

/**
 * End-to-end tests against a running stack (gateway + services), e.g.
 *   docker compose -f deploy/docker-compose.yml --env-file deploy/.env up --build -d
 *   E2E_BASE_URL=http://localhost:8080 E2E_DEMO_PASSWORD=... npm run e2e
 *
 * The tests register real accounts and change real connections, so they run one at a time.
 * The gateway rate-limits login/register (10/min per IP); the whole suite stays below that.
 */
const baseURL = process.env.E2E_BASE_URL ?? 'http://localhost:8080';
// Optional: a Chromium binary to use instead of the one Playwright manages.
const executablePath = process.env.E2E_CHROMIUM_PATH;

export default defineConfig({
  testDir: './e2e',
  fullyParallel: false,
  workers: 1,
  forbidOnly: Boolean(process.env.CI),
  retries: process.env.CI ? 1 : 0,
  timeout: 90_000,
  expect: { timeout: 15_000 },
  reporter: process.env.CI ? [['list'], ['html', { open: 'never' }]] : [['list']],
  use: {
    baseURL,
    trace: 'retain-on-failure',
    screenshot: 'only-on-failure',
    video: 'off',
  },
  projects: [
    {
      name: 'chromium',
      use: {
        ...devices['Desktop Chrome'],
        ...(executablePath ? { launchOptions: { executablePath } } : {}),
      },
    },
  ],
});
