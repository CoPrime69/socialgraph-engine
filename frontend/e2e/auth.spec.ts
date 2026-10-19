import AxeBuilder from '@axe-core/playwright';
import { expect, test } from '@playwright/test';
import { expectOverview, signIn, uniqueHandle } from './helpers';

test('a wrong password is rejected without signing in', async ({ page }) => {
  await signIn(page, uniqueHandle('nobody'), 'definitely-not-the-password');
  await expect(
    page.getByRole('alert').filter({ hasText: 'Incorrect handle or password.' }),
  ).toBeVisible();
  await expect(page).toHaveURL(/\/sign-in$/);
  await expect(page.getByLabel('Password', { exact: true })).toBeFocused();
});

test('protected pages send anonymous visitors to sign in', async ({ page }) => {
  await page.goto('/people?q=jon');
  await expect(page).toHaveURL(/\/sign-in$/);
  await expect(page.getByRole('heading', { level: 1, name: 'Sign in' })).toBeVisible();
});

test('the sign-in page has no serious accessibility violations', async ({ page }) => {
  await page.goto('/sign-in');
  await expect(page.getByRole('heading', { level: 1, name: 'Sign in' })).toBeVisible();
  const results = await new AxeBuilder({ page }).withTags(['wcag2a', 'wcag2aa']).analyze();
  const serious = results.violations.filter(
    (violation) => violation.impact === 'serious' || violation.impact === 'critical',
  );
  expect(serious.map((violation) => violation.id)).toEqual([]);
});

test.describe('seeded demo member', () => {
  const password = process.env.E2E_DEMO_PASSWORD;
  test.skip(!password, 'Set E2E_DEMO_PASSWORD to the SEED_DEMO_PASSWORD of the stack.');

  test('sees their 20 seeded connections, topics and an accessible overview', async ({ page }) => {
    await signIn(page, 'demo', password ?? '');
    await expectOverview(page);
    const results = await new AxeBuilder({ page })
      .withTags(['wcag2a', 'wcag2aa'])
      .exclude('canvas')
      .analyze();
    const serious = results.violations.filter(
      (violation) => violation.impact === 'serious' || violation.impact === 'critical',
    );
    expect(serious.map((violation) => violation.id)).toEqual([]);

    await page
      .getByRole('navigation', { name: 'Main' })
      .getByRole('link', { name: 'My network' })
      .click();
    await expect(page.getByText(/^20 people, strongest connection first$/)).toBeVisible({
      timeout: 30_000,
    });

    await page
      .getByRole('navigation', { name: 'Main' })
      .getByRole('link', { name: 'Topics' })
      .click();
    const firstQuestion = page.getByRole('main').getByRole('listitem').getByRole('link').first();
    await expect(firstQuestion).toBeVisible();
    await page
      .getByRole('link', { name: /^javascript/ })
      .first()
      .click();
    await expect(page.getByRole('heading', { name: /Questions tagged/ })).toBeVisible();
    await page.locator('a[href^="/questions/"]').first().click();
    const external = page.getByRole('link', { name: /Read on Stack Overflow/ });
    await expect(external).toHaveAttribute('href', /^https:\/\/stackoverflow\.com\//);
    await expect(external).toHaveAttribute('rel', 'noopener noreferrer');
  });
});
