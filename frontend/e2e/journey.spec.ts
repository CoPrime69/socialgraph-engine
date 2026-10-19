import { expect, test } from '@playwright/test';
import { expectOverview, PASSWORD, parseCount, register, signIn, uniqueHandle } from './helpers';

/**
 * The main journey of a new member, against the real stack: register, read the real network
 * statistics, find someone, see the degrees of separation, connect from the suggestions, see
 * the change everywhere (read-your-writes), remove it again, sign out and back in.
 */
test('a new member explores the network and manages a connection', async ({ page }) => {
  const handle = uniqueHandle();
  const name = 'E2E Explorer';

  await test.step('register', async () => {
    await register(page, handle, name);
    await expectOverview(page);
    await expect(page.getByRole('link', { name: new RegExp(`@${handle}`) })).toBeVisible();
  });

  let influencerHandle = '';
  await test.step('overview shows the real network', async () => {
    const stats = page.locator('dl[aria-label="Network statistics"]');
    await expect(stats).toBeVisible({ timeout: 30_000 });
    const people = stats
      .locator('div', { hasText: /^People/ })
      .locator('dd')
      .first();
    expect(parseCount(await people.textContent())).toBeGreaterThan(18_000);
    const firstRow = page
      .getByRole('table', { name: /Most influential people/ })
      .getByRole('row')
      .nth(1);
    await expect(firstRow).toBeVisible();
    influencerHandle = /@([a-z0-9_]+)/.exec(await firstRow.innerText())?.[1] ?? '';
    expect(influencerHandle).not.toBe('');
    await expect(page.locator('canvas[role="img"]')).toBeVisible({ timeout: 30_000 });
  });

  await test.step('find the top influencer and see the degrees of separation', async () => {
    await page
      .getByRole('navigation', { name: 'Main' })
      .getByRole('link', { name: 'People' })
      .click();
    await page.getByLabel('Search', { exact: true }).fill(influencerHandle);
    const result = page
      .getByRole('table')
      .getByRole('link', { name: new RegExp(`@${influencerHandle}\\b`) })
      .first();
    await expect(result).toBeVisible();
    await result.click();
    await expect(page.getByRole('heading', { level: 1 })).toBeVisible();
    await expect(page.getByText(`@${influencerHandle}`, { exact: false }).first()).toBeVisible();
    // A brand-new member has no connections yet, so nobody is reachable.
    await expect(page.getByText(/not connected to .* through the network yet/)).toBeVisible({
      timeout: 30_000,
    });
    await expect(page.getByRole('heading', { name: 'Stack Overflow activity' })).toBeVisible();
    await expect(page.getByRole('heading', { name: 'Top tags' })).toBeVisible();
  });

  let suggested = '';
  await test.step('connect with a suggested person', async () => {
    await page
      .getByRole('navigation', { name: 'Main' })
      .getByRole('link', { name: 'My network' })
      .click();
    const connect = page.getByRole('button', { name: /^Connect with / }).first();
    await expect(connect).toBeVisible({ timeout: 30_000 });
    suggested = ((await connect.getAttribute('aria-label')) ?? '').replace(/^Connect with /, '');
    expect(suggested).not.toBe('');
    await connect.click();
    await expect(
      page.getByRole('status').filter({ hasText: `Connected with ${suggested}` }),
    ).toBeVisible();
    const connections = page.getByRole('region', { name: 'Your connections' });
    await expect(connections.getByRole('link', { name: new RegExp(suggested) })).toBeVisible();
  });

  await test.step('the change is visible in analytics right away', async () => {
    await page
      .getByRole('region', { name: 'Your connections' })
      .getByRole('link', { name: new RegExp(suggested) })
      .click();
    await expect(page.getByRole('heading', { level: 1, name: suggested })).toBeVisible();
    await expect(page.getByText(`You are directly connected to ${suggested}.`)).toBeVisible({
      timeout: 30_000,
    });
    await expect(page.getByRole('button', { name: 'Remove connection' })).toBeVisible();
  });

  await test.step('remove the connection', async () => {
    await page
      .getByRole('navigation', { name: 'Main' })
      .getByRole('link', { name: 'My network' })
      .click();
    await page.getByRole('button', { name: `Remove connection with ${suggested}` }).click();
    const dialog = page.getByRole('dialog', { name: `Remove ${suggested} from your network?` });
    await expect(dialog).toBeVisible();
    await dialog.getByRole('button', { name: 'Remove connection' }).click();
    await expect(dialog).toBeHidden();
    await expect(
      page.getByRole('status').filter({ hasText: `Removed your connection with ${suggested}` }),
    ).toBeVisible();
    await expect(page.getByText("You haven't connected with anyone yet")).toBeVisible();
  });

  await test.step('sign out and sign back in', async () => {
    await page.getByRole('button', { name: 'Sign out' }).click();
    await expect(page.getByRole('heading', { level: 1, name: 'Sign in' })).toBeVisible();
    await signIn(page, handle, PASSWORD);
    await expectOverview(page);
  });
});
