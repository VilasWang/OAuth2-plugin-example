import { test, expect } from '@playwright/test'
import { setupAuthenticatedMocks, loginAsAdmin, MOCK_LOGS } from './helpers/mock-api'

test.describe('Audit Logs', () => {
  test.beforeEach(async ({ page }) => {
    await setupAuthenticatedMocks(page)
    await loginAsAdmin(page)
    await page.click('nav a:has-text("Audit Logs")')
    await page.waitForURL('**/admin/logs')
  })

  test('displays audit logs page title', async ({ page }) => {
    await expect(page.locator('h2')).toContainText('Audit Logs')
  })

  test('shows log entries with correct columns', async ({ page }) => {
    await expect(page.locator('th:has-text("Time")')).toBeVisible()
    await expect(page.locator('th:has-text("Action")')).toBeVisible()
    await expect(page.locator('th:has-text("Actor")')).toBeVisible()
    await expect(page.locator('th:has-text("Outcome")')).toBeVisible()
    await expect(page.locator('th:has-text("IP")')).toBeVisible()
  })

  test('displays log actions', async ({ page }) => {
    await expect(page.locator('text=login_success')).toBeVisible()
    await expect(page.locator('text=token_issued')).toBeVisible()
    await expect(page.locator('text=login_failure')).toBeVisible()
  })

  test('shows outcome badges with correct colors', async ({ page }) => {
    // Success badges (green)
    const successBadges = page.locator('span.bg-green-100:has-text("success")')
    await expect(successBadges.first()).toBeVisible()

    // Failure badges (red)
    const failureBadges = page.locator('span.bg-red-100:has-text("failure")')
    await expect(failureBadges.first()).toBeVisible()
  })

  test('displays IP addresses', async ({ page }) => {
    await expect(page.locator('text=127.0.0.1').first()).toBeVisible()
    await expect(page.locator('text=192.168.1.100')).toBeVisible()
  })

  test('shows pagination controls', async ({ page }) => {
    await expect(page.locator('text=Page 1')).toBeVisible()
    await expect(page.locator('text=Previous')).toBeVisible()
    await expect(page.locator('text=Next')).toBeVisible()
  })

  test('previous button is disabled on first page', async ({ page }) => {
    // The "Previous" button uses :disabled attribute when page <= 1
    const prevBtn = page.locator('button:has-text("Previous")')
    await expect(prevBtn).toBeDisabled()
  })

  test('next button disabled when fewer than 50 results', async ({ page }) => {
    // MOCK_LOGS has 3 entries (< 50), so next should be disabled
    const nextBtn = page.locator('button:has-text("Next")')
    await expect(nextBtn).toBeDisabled()
  })

  test('shows empty state when no logs', async ({ page }) => {
    await page.route('**/api/admin/logs**', async (route) => {
      await route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify({ logs: [] }),
      })
    })

    // Navigate away and back to trigger fresh fetch
    await page.click('nav a:has-text("Dashboard")')
    await page.click('nav a:has-text("Audit Logs")')
    await page.waitForURL('**/admin/logs')
    await expect(page.locator('text=No audit logs recorded yet')).toBeVisible()
  })

  test('pagination sends correct page parameter', async ({ page }) => {
    // Override with 50 logs to enable next button
    const manyLogs = Array.from({ length: 50 }, (_, i) => ({
      id: i + 1,
      action: 'login_success',
      actor_type: 'user',
      actor_id: '550e8400-e29b-41d4-a716-446655440000',
      outcome: 'success',
      ip: '127.0.0.1',
      timestamp: '2026-05-21T10:00:00Z',
    }))

    let requestedPage = 1
    await page.route('**/api/admin/logs**', async (route) => {
      const url = new URL(route.request().url())
      requestedPage = parseInt(url.searchParams.get('page') || '1')
      await route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify({ logs: requestedPage === 1 ? manyLogs : MOCK_LOGS }),
      })
    })

    // Navigate away and back to get the 50-item response
    await page.click('nav a:has-text("Dashboard")')
    await page.click('nav a:has-text("Audit Logs")')
    await page.waitForURL('**/admin/logs')

    // Next should be enabled now (50 results)
    const nextBtn = page.locator('button:has-text("Next")')
    await expect(nextBtn).not.toBeDisabled()
    await nextBtn.click()
    await expect(page.locator('text=Page 2')).toBeVisible()
    expect(requestedPage).toBe(2)
  })
})
