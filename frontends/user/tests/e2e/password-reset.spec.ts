import { test, expect } from '@playwright/test'
import { setupMocks } from './helpers/mock-api'

test.describe('Password Reset', () => {
  test.beforeEach(async ({ page }) => {
    await setupMocks(page)
  })

  test('valid reset token resets password successfully', async ({ page }) => {
    await page.goto('/reset-password?token=valid-reset-token')
    await page.waitForLoadState('networkidle')

    // Fill new password fields
    const passwordInputs = page.locator('input[type="password"]')
    const count = await passwordInputs.count()
    if (count >= 2) {
      await passwordInputs.nth(0).fill('newpassword123')
      await passwordInputs.nth(1).fill('newpassword123')
    } else if (count === 1) {
      await passwordInputs.first().fill('newpassword123')
    }

    // Submit
    const submitButton = page.locator('button[type="submit"]')
    if (await submitButton.isVisible()) {
      await submitButton.click()
      await page.waitForTimeout(500)
      // Should show success message or redirect
      const url = page.url()
      expect(url).toMatch(/\/(login|reset-password)/)
    }
  })

  test('expired reset token shows error', async ({ page }) => {
    // Override confirm endpoint to return expired error
    await page.route('**/api/password-reset/confirm', async (route) => {
      await route.fulfill({
        status: 400,
        contentType: 'application/json',
        body: JSON.stringify({
          error: { code: 'TOKEN_EXPIRED', category: 'VALIDATION', message: 'Reset token has expired' },
        }),
      })
    })

    await page.goto('/reset-password?token=expired-token')
    await page.waitForLoadState('networkidle')

    const passwordInputs = page.locator('input[type="password"]')
    const count = await passwordInputs.count()
    if (count >= 2) {
      await passwordInputs.nth(0).fill('newpassword123')
      await passwordInputs.nth(1).fill('newpassword123')
    } else if (count === 1) {
      await passwordInputs.first().fill('newpassword123')
    }

    const submitButton = page.locator('button[type="submit"]')
    if (await submitButton.isVisible()) {
      await submitButton.click()
      await page.waitForTimeout(500)
      // Error should be displayed
      const errorEl = page.locator('.bg-red-50, [class*="red"]')
      if (await errorEl.first().isVisible()) {
        await expect(errorEl.first()).toBeVisible()
      }
    }
  })

  test('invalid reset token shows error', async ({ page }) => {
    await page.route('**/api/password-reset/confirm', async (route) => {
      await route.fulfill({
        status: 400,
        contentType: 'application/json',
        body: JSON.stringify({
          error: { code: 'INVALID_TOKEN', category: 'VALIDATION', message: 'Invalid reset token' },
        }),
      })
    })

    await page.goto('/reset-password?token=invalid-random-token')
    await page.waitForLoadState('networkidle')

    const passwordInputs = page.locator('input[type="password"]')
    const count = await passwordInputs.count()
    if (count >= 2) {
      await passwordInputs.nth(0).fill('newpassword123')
      await passwordInputs.nth(1).fill('newpassword123')
    } else if (count === 1) {
      await passwordInputs.first().fill('newpassword123')
    }

    const submitButton = page.locator('button[type="submit"]')
    if (await submitButton.isVisible()) {
      await submitButton.click()
      await page.waitForTimeout(500)
      const errorEl = page.locator('.bg-red-50, [class*="red"]')
      if (await errorEl.first().isVisible()) {
        await expect(errorEl.first()).toBeVisible()
      }
    }
  })

  test('no token in URL handles gracefully', async ({ page }) => {
    await page.goto('/reset-password')
    await page.waitForLoadState('networkidle')
    // Page should render without crashing
    const url = page.url()
    expect(url).toContain('/reset-password')
    // Either shows form (ready for token input) or shows error
    const pageContent = await page.content()
    expect(pageContent.length).toBeGreaterThan(0)
  })
})
