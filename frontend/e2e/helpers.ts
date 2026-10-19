import { expect, type Page } from '@playwright/test';

export const PASSWORD = 'correct horse battery staple';

/** A fresh, valid handle (^[a-z0-9_]{3,32}$) so every run registers a new member. */
export function uniqueHandle(prefix = 'e2e'): string {
  const time = Date.now().toString(36);
  const random = Math.floor(Math.random() * 36 ** 4)
    .toString(36)
    .padStart(4, '0');
  return `${prefix}_${time}_${random}`;
}

export async function register(page: Page, handle: string, name: string): Promise<void> {
  await page.goto('/register');
  await page.getByLabel('Handle').fill(handle);
  await page.getByLabel('Display name').fill(name);
  await page.getByLabel('Password', { exact: true }).fill(PASSWORD);
  const category = page.getByLabel('Category');
  await expect(category).toBeEnabled();
  await category.selectOption({ index: 1 });
  await page.getByLabel('Primary technology').selectOption({ index: 1 });
  await page.getByRole('button', { name: 'Create account' }).click();
}

export async function signIn(page: Page, handle: string, password: string): Promise<void> {
  await page.goto('/sign-in');
  await page.getByLabel('Handle').fill(handle);
  await page.getByLabel('Password', { exact: true }).fill(password);
  await page.getByRole('button', { name: 'Sign in' }).click();
}

export async function expectOverview(page: Page): Promise<void> {
  // The first page after sign-up may wait for the profile to be provisioned.
  await expect(page.getByRole('heading', { level: 1, name: 'Network overview' })).toBeVisible({
    timeout: 45_000,
  });
}

/** Parses a formatted integer such as "18,649". */
export function parseCount(text: string | null): number {
  return Number((text ?? '').replace(/[^\d]/g, ''));
}
