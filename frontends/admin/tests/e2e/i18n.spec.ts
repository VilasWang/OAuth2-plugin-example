// i18n acceptance specs (ADR-0013): locale detection, switcher behavior,
// error-catalog locale following, and fallback for bogus stored values.
// The Playwright config pins the browser locale to en-US; zh-CN paths are
// exercised explicitly here.
import { expect, test } from '@playwright/test'
import { setupAuthenticatedMocks } from './helpers/mock-api'

test.describe('i18n (ADR-0013)', () => {
  test('defaults to English with <html lang> synced', async ({ page }) => {
    await setupAuthenticatedMocks(page)
    await page.goto('/admin/login')
    await expect(page.locator('html')).toHaveAttribute('lang', 'en')
    await expect(page.getByRole('heading', { name: /sign in to fulla admin/i })).toBeVisible()
  })

  test('switcher switches chrome to zh-CN, syncs <html lang>, and persists', async ({ page }) => {
    await setupAuthenticatedMocks(page)
    await page.goto('/admin/login')

    await page.getByRole('button', { name: 'Language' }).click()
    await page.getByRole('menuitemradio', { name: '简体中文' }).click()

    await expect(page.locator('html')).toHaveAttribute('lang', 'zh-CN')
    await expect(page.getByRole('heading', { name: '登录 Fulla 管理控制台' })).toBeVisible()

    // Persistence: the stored choice survives a full reload.
    await page.reload()
    await expect(page.getByRole('heading', { name: '登录 Fulla 管理控制台' })).toBeVisible()
    await expect(page.locator('html')).toHaveAttribute('lang', 'zh-CN')
  })

  test('error messages follow the UI locale (zh-CN session)', async ({ page }) => {
    await setupAuthenticatedMocks(page)
    // Later route registrations take precedence over the helper's default.
    await page.route('**/oauth2/login', async (route) => {
      await route.fulfill({
        status: 401,
        contentType: 'application/json',
        body: JSON.stringify({
          error: { code: 'AUTH_INVALID_CREDENTIALS', request_id: 'req-i18n-zh' },
        }),
      })
    })
    await page.addInitScript(() => localStorage.setItem('fulla-locale', 'zh-CN'))

    await page.goto('/admin/login')
    await page.fill('input[type="text"]', 'wrong')
    await page.fill('input[type="password"]', 'wrong')
    await page.click('button[type="submit"]')
    await expect(page.locator('[role="alert"]')).toContainText('用户名或密码错误')
  })

  test('error messages default to English (en session)', async ({ page }) => {
    await setupAuthenticatedMocks(page)
    await page.route('**/oauth2/login', async (route) => {
      await route.fulfill({
        status: 401,
        contentType: 'application/json',
        body: JSON.stringify({
          error: { code: 'AUTH_INVALID_CREDENTIALS', request_id: 'req-i18n-en' },
        }),
      })
    })

    await page.goto('/admin/login')
    await page.fill('input[type="text"]', 'wrong')
    await page.fill('input[type="password"]', 'wrong')
    await page.click('button[type="submit"]')
    await expect(page.locator('[role="alert"]')).toContainText('Incorrect username or password')
  })

  test('bogus stored locale falls back to English', async ({ page }) => {
    await setupAuthenticatedMocks(page)
    await page.addInitScript(() => localStorage.setItem('fulla-locale', 'xx-XX'))
    await page.goto('/admin/login')
    await expect(page.locator('html')).toHaveAttribute('lang', 'en')
    await expect(page.getByRole('heading', { name: /sign in to fulla admin/i })).toBeVisible()
  })

  // Locks the documented ADR-0013 limitation: error messages resolve once,
  // at trigger time. Switching locale afterwards does NOT re-translate text
  // already on screen; only newly triggered errors follow the new locale.
  // (A fully reactive variant is a tracked follow-up — do not "fix" this
  // test casually; change it together with the implementation.)
  test('already-surfaced error text is a snapshot across a locale switch', async ({ page }) => {
    await setupAuthenticatedMocks(page)
    await page.route('**/oauth2/login', async (route) => {
      await route.fulfill({
        status: 401,
        contentType: 'application/json',
        body: JSON.stringify({
          error: { code: 'AUTH_INVALID_CREDENTIALS', request_id: 'req-i18n-snap' },
        }),
      })
    })

    await page.goto('/admin/login')
    await page.fill('input[type="text"]', 'wrong')
    await page.fill('input[type="password"]', 'wrong')
    await page.click('button[type="submit"]')
    const alert = page.locator('[role="alert"]')
    await expect(alert).toContainText('Incorrect username or password')

    // Switch to zh-CN: page chrome follows...
    await page.getByRole('button', { name: 'Language' }).click()
    await page.getByRole('menuitemradio', { name: '简体中文' }).click()
    await expect(page.getByRole('heading', { name: '登录 Fulla 管理控制台' })).toBeVisible()

    // ...but the already-rendered error keeps the locale it was resolved in.
    await expect(alert).toContainText('Incorrect username or password')

    // A newly triggered error resolves in the active locale.
    await page.click('button[type="submit"]')
    await expect(alert).toContainText('用户名或密码错误')
  })
})
