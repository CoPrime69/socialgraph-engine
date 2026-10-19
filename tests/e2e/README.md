# End-to-end tests

The Playwright specs live in [`frontend/e2e/`](../../frontend/e2e) next to their configuration
([`frontend/playwright.config.ts`](../../frontend/playwright.config.ts)), so they use the
frontend's pinned `@playwright/test` and TypeScript setup without a root `package.json`.

```sh
./scripts/generate-env.sh                       # once
docker compose -f deploy/docker-compose.yml --env-file deploy/.env up --build -d --wait
cd frontend
npm ci
npx playwright install chromium                  # skip if browsers are preinstalled
E2E_BASE_URL=http://localhost:8080 E2E_DEMO_PASSWORD=<SEED_DEMO_PASSWORD> npm run e2e
```

See `frontend/README.md` (section "End-to-end tests") for what the journeys cover.
