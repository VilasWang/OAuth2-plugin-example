// i18n acceptance specs (ADR-0013): locale detection, switcher behavior,
// error-catalog locale following, and fallback for bogus stored values.
// The Playwright config pins the browser locale to en-US; zh-CN paths are
// exercised explicitly here.
import { expect, test } from '@playwright/test'
import { setupMocks } from './helpers/mock-api'

test.describe('i18n (ADR-0013)', () => {
  test('defaults to English with <html lang> synced', async ({ page }) => {
    await setupMocks(page)
    await page.goto('/login')
    await expect(page.locator('html')).toHaveAttribute('lang', 'en')
    await expect(page.getByRole('heading', { name: /sign in to your account/i })).toBeVisible()
  })

  test('switcher switches chrome to zh-CN, syncs <html lang>, and persists', async ({ page }) => {
    await setupMocks(page)
    await page.goto('/login')

    await page.getByRole('button', { name: 'Language' }).click()
    await page.getByRole('menuitemradio', { name: '简体中文' }).click()

    await expect(page.locator('html')).toHaveAttribute('lang', 'zh-CN')
    await expect(page.getByRole('heading', { name: '登录您的账户' })).toBeVisible()

    // Persistence: the stored choice survives a full reload.
    await page.reload()
    await expect(page.getByRole('heading', { name: '登录您的账户' })).toBeVisible()
    await expect(page.locator('html')).toHaveAttribute('lang', 'zh-CN')
  })

  test('error messages follow the UI locale (zh-CN session)', async ({ page }) => {
    await setupMocks(page)
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

    await page.goto('/login')
    await page.locator('input[autocomplete="username"]').fill('testuser')
    await page.locator('input[autocomplete="current-password"]').fill('wrong')
    await page.locator('button[type="submit"]').click()
    await expect(page.locator('[role="alert"]')).toContainText('用户名或密码错误')
  })

  test('error messages default to English (en session)', async ({ page }) => {
    await setupMocks(page)
    await page.route('**/oauth2/login', async (route) => {
      await route.fulfill({
        status: 401,
        contentType: 'application/json',
        body: JSON.stringify({
          error: { code: 'AUTH_INVALID_CREDENTIALS', request_id: 'req-i18n-en' },
        }),
      })
    })

    await page.goto('/login')
    await page.locator('input[autocomplete="username"]').fill('testuser')
    await page.locator('input[autocomplete="current-password"]').fill('wrong')
    await page.locator('button[type="submit"]').click()
    await expect(page.locator('[role="alert"]')).toContainText('Incorrect username or password')
  })

  test('bogus stored locale falls back to English', async ({ page }) => {
    await setupMocks(page)
    await page.addInitScript(() => localStorage.setItem('fulla-locale', 'xx-XX'))
    await page.goto('/login')
    await expect(page.locator('html')).toHaveAttribute('lang', 'en')
    await expect(page.getByRole('heading', { name: /sign in to your account/i })).toBeVisible()
  })
})
