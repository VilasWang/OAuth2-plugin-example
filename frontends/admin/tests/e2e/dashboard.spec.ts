import { test, expect } from '@playwright/test'
import { setupAuthenticatedMocks, loginAsAdmin } from './helpers/mock-api'

test.describe('Dashboard', () => {
  test.beforeEach(async ({ page }) => {
    await setupAuthenticatedMocks(page)
    await loginAsAdmin(page)
  })

  test('displays stats cards with real data', async ({ page }) => {
    await expect(page.locator('text=Total Users')).toBeVisible()
    await expect(page.locator('p:has-text("Applications")').first()).toBeVisible()
    await expect(page.locator('text=Active Tokens')).toBeVisible()
    await expect(page.locator('text=Failures Today')).toBeVisible()
    // Check actual values from mock
    await expect(page.locator('p.text-3xl:has-text("5")').first()).toBeVisible()  // total_users
    await expect(page.locator('p.text-3xl:has-text("12")')).toBeVisible()  // active_tokens
  })

  test('displays system health status', async ({ page }) => {
    await expect(page.locator('text=System Status')).toBeVisible()
    await expect(page.locator('text=Healthy')).toBeVisible()
  })

  test('shows database connection status', async ({ page }) => {
    await expect(page.locator('text=Database')).toBeVisible()
    await expect(page.locator('text=connected').first()).toBeVisible()
  })

  test('shows Redis connection status', async ({ page }) => {
    await expect(page.locator('text=Redis')).toBeVisible()
  })

  test('displays quick action links', async ({ page }) => {
    await expect(page.locator('text=Quick Actions')).toBeVisible()
    const main = page.locator('main')
    await expect(main.locator('a[href="/admin/applications"] p')).toBeVisible()
    await expect(main.locator('a[href="/admin/users"] p')).toBeVisible()
    await expect(main.locator('a[href="/admin/roles"] p')).toBeVisible()
    await expect(main.locator('a[href="/admin/scopes"] p')).toBeVisible()
  })

  test('quick action links navigate correctly', async ({ page }) => {
    await page.locator('main a:has-text("Applications")').click()
    await expect(page).toHaveURL(/\/admin\/applications/)

    await page.goBack()
    await page.locator('main a:has-text("Users")').click()
    await expect(page).toHaveURL(/\/admin\/users/)

    await page.goBack()
    await page.locator('main a:has-text("Roles")').click()
    await expect(page).toHaveURL(/\/admin\/roles/)

    await page.goBack()
    await page.locator('main a:has-text("Scopes")').click()
    await expect(page).toHaveURL(/\/admin\/scopes/)
  })
})

test.describe('Dashboard - unhealthy state', () => {
  test('shows unhealthy status when backend is down', async ({ page }) => {
    await setupAuthenticatedMocks(page)
    await page.route('**/health/ready', async (route) => {
      await route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify({ status: 'error', message: 'Database unreachable' }),
      })
    })
    await loginAsAdmin(page)
    await expect(page.locator('text=Unhealthy')).toBeVisible()
  })

  // A-DASH-006: stats API failure must surface a descriptive error banner.
  // The Frontend_Error_Module (normalizeError) maps the backend error CODE to
  // a localized message — INTERNAL_ERROR → "服务器内部错误" — rather than
  // echoing the raw backend message string (Requirement 8.6). We assert the
  // banner renders the localized mapping, not the raw payload.
  test('shows descriptive error banner when stats API fails (A-DASH-006)', async ({ page }) => {
    await setupAuthenticatedMocks(page)
    await page.route('**/api/admin/dashboard/stats', async (route) => {
      await route.fulfill({
        status: 500,
        contentType: 'application/json',
        body: JSON.stringify({
          error: { code: 'INTERNAL_ERROR', category: 'SYSTEM', message: 'Stats query failed' },
        }),
      })
    })
    await loginAsAdmin(page)
    const banner = page.locator('.bg-red-50')
    await expect(banner).toBeVisible()
    // INTERNAL_ERROR is mapped to its localized message; the raw backend
    // message is intentionally NOT surfaced to the UI.
    await expect(banner).toContainText('服务器内部错误')
    await expect(banner).not.toContainText('Stats query failed')
  })
})

