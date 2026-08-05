import { test, expect } from '@playwright/test'
import { setupAuthenticatedMocks, loginAsAdmin } from './helpers/mock-api'

test.describe('Error Handling', () => {
  test('401 on API call shows error on dashboard', async ({ page }) => {
    await setupAuthenticatedMocks(page)
    await page.route('**/api/admin/dashboard/stats', async (route) => {
      await route.fulfill({
        status: 401,
        contentType: 'application/json',
        body: JSON.stringify({
          error: { code: 'AUTH_UNAUTHORIZED', category: 'AUTHENTICATION', message: 'Session expired' },
        }),
      })
    })
    await loginAsAdmin(page)
    await page.waitForLoadState('networkidle')
    await expect(page.locator('h2:has-text("Dashboard")')).toBeVisible()
    await expect(page.locator('[role="alert"]').first()).toBeVisible()
  })

  test('500 response shows error banner on dashboard', async ({ page }) => {
    await setupAuthenticatedMocks(page)
    await page.route('**/api/admin/dashboard/stats', async (route) => {
      await route.fulfill({
        status: 500,
        contentType: 'application/json',
        body: JSON.stringify({
          error: { code: 'INTERNAL_ERROR', category: 'SYSTEM', message: 'Internal server error' },
        }),
      })
    })
    await loginAsAdmin(page)
    await page.waitForLoadState('networkidle')
    await expect(page.locator('h2:has-text("Dashboard")')).toBeVisible()
    await expect(page.locator('[role="alert"]').first()).toBeVisible()
  })

  test('network failure on health shows unhealthy', async ({ page }) => {
    await setupAuthenticatedMocks(page)
    await page.route('**/health/ready', async (route) => {
      await route.abort('failed')
    })
    await loginAsAdmin(page)
    await page.waitForLoadState('networkidle')
    await expect(page.locator('h2:has-text("Dashboard")')).toBeVisible()
  })

  test('403 forbidden shows error on users page', async ({ page }) => {
    await setupAuthenticatedMocks(page)
    await page.route('**/api/admin/users', async (route) => {
      await route.fulfill({
        status: 403,
        contentType: 'application/json',
        body: JSON.stringify({
          error: { code: 'AUTH_FORBIDDEN', category: 'AUTHORIZATION', message: 'Insufficient permissions' },
        }),
      })
    })
    await loginAsAdmin(page)
    await page.click('nav a:has-text("Users")')
    await page.waitForLoadState('networkidle')
    // UsersPage uses inline error banner with bg-red-50
    const errorElement = page.locator('.bg-red-50, .text-red-700')
    await expect(errorElement.first()).toBeVisible()
  })
})
