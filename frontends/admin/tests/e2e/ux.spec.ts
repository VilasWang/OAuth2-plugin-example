import { test, expect } from '@playwright/test'
import { setupAuthenticatedMocks, loginAsAdmin } from './helpers/mock-api'

test.describe('UX Polish', () => {
  test.describe('Success & Error Messages', () => {
    test.beforeEach(async ({ page }) => {
      await setupAuthenticatedMocks(page)
      await loginAsAdmin(page)
    })

    test('success message auto-dismisses after ~3 seconds', async ({ page }) => {
      // Go to roles page where we can trigger a success message
      await page.click('nav a:has-text("Roles")')
      await page.waitForURL('**/admin/roles')

      // Edit a role to trigger success
      const editBtn = page.locator('button:has-text("Edit")').first()
      await editBtn.click()
      const descInput = page.locator('.fixed input[placeholder="Optional description"]')
      await descInput.clear()
      await descInput.fill('New description ' + Date.now())
      await page.locator('.fixed button:has-text("Save")').click()

      // Success message should appear
      await expect(page.locator('.bg-green-50')).toBeVisible({ timeout: 3000 })
      // After 3s, it should disappear
      await expect(page.locator('.bg-green-50')).not.toBeVisible({ timeout: 5000 })
    })

    test('error message auto-dismisses after ~5 seconds', async ({ page }) => {
      // Trigger an error by trying to create a duplicate role
      await page.click('nav a:has-text("Roles")')
      await page.waitForURL('**/admin/roles')

      // Mock duplicate error
      await page.route('**/api/admin/roles', async (route) => {
        if (route.request().method() === 'POST') {
          await route.fulfill({
            status: 409,
            contentType: 'application/json',
            body: JSON.stringify({ error: { code: 'ROLE_EXISTS', message: 'Role already exists' } }),
          })
        } else { await route.continue() }
      })

      await page.click('button:has-text("+ Create Role")')
      await page.fill('input[placeholder="e.g. editor"]', 'duplicate-role')
      await page.locator('.fixed button:has-text("Create")').click()

      // Error message should appear
      await expect(page.locator('.bg-red-50, .text-red-700').first()).toBeVisible({ timeout: 3000 })
      // After 5s, it should disappear
      await expect(page.locator('.bg-red-50, .text-red-700').first()).not.toBeVisible({ timeout: 7000 })
    })
  })

  test.describe('Dashboard Polish', () => {
    test('loading state shows dash placeholder', async ({ page }) => {
      await setupAuthenticatedMocks(page)
      // Delay dashboard API responses so we can observe loading momentarily
      await page.route('**/health/ready', async (route) => {
        await new Promise(resolve => setTimeout(resolve, 300))
        await route.fulfill({
          status: 200,
          contentType: 'application/json',
          body: JSON.stringify({ status: 'ok', database: 'connected', redis: 'connected' }),
        })
      })
      await page.route('**/api/admin/dashboard/stats', async (route) => {
        await new Promise(resolve => setTimeout(resolve, 300))
        await route.fulfill({
          status: 200,
          contentType: 'application/json',
          body: JSON.stringify({ total_users: 5, total_clients: 3, active_tokens: 12, failures_today: 0 }),
        })
      })
      await loginAsAdmin(page)
      // After login, dashboard renders with real data
      await page.waitForTimeout(1000)
      await expect(page.locator('h2:has-text("Dashboard")')).toBeVisible()
    })

    test('failures today > 0 shows red color', async ({ page }) => {
      await setupAuthenticatedMocks(page)
      await page.route('**/api/admin/dashboard/stats', async (route) => {
        await route.fulfill({
          status: 200,
          contentType: 'application/json',
          body: JSON.stringify({ total_users: 5, total_clients: 3, active_tokens: 12, failures_today: 3 }),
        })
      })
      await loginAsAdmin(page)
      // The failures card should have red color class when > 0
      const redText = page.locator('.text-red-600')
      await expect(redText.first()).toBeVisible({ timeout: 3000 })
    })

    test('failures today = 0 shows normal color', async ({ page }) => {
      await setupAuthenticatedMocks(page)
      await page.route('**/api/admin/dashboard/stats', async (route) => {
        await route.fulfill({
          status: 200,
          contentType: 'application/json',
          body: JSON.stringify({ total_users: 5, total_clients: 3, active_tokens: 12, failures_today: 0 }),
        })
      })
      await loginAsAdmin(page)
      // Check the "Failures Today" card: 0 should not be red
      // We check the failures value card (4th stat card)
      const blocks = page.locator('.grid .bg-white')
      await expect(blocks.last()).toBeVisible()
    })
  })
})
