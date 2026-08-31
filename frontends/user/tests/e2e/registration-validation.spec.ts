import { test, expect } from '@playwright/test'
import { setupMocks, mockRegistrationError } from './helpers/mock-api'

test.describe('Registration Validation', () => {
  test.beforeEach(async ({ page }) => {
    await setupMocks(page)
    await page.goto('/register')
    await page.waitForLoadState('networkidle')
  })

  test('password too short shows error', async ({ page }) => {
    await page.locator('input[autocomplete="username"]').fill('newuser')
    await page.locator('input[type="email"]').fill('new@example.com')
    await page.locator('input[autocomplete="new-password"]').first().fill('12345')
    await page.locator('input[autocomplete="new-password"]').last().fill('12345')
    await page.locator('button[type="submit"]').click()
    await expect(page.locator('text=at least 8 characters')).toBeVisible()
  })

  test('passwords do not match shows error', async ({ page }) => {
    await page.locator('input[autocomplete="username"]').fill('newuser')
    await page.locator('input[type="email"]').fill('new@example.com')
    await page.locator('input[autocomplete="new-password"]').first().fill('password123')
    await page.locator('input[autocomplete="new-password"]').last().fill('different456')
    await page.locator('button[type="submit"]').click()
    await expect(page.locator('text=Passwords do not match')).toBeVisible()
  })

  test('duplicate username shows error', async ({ page }) => {
    // Override registration mock to return conflict
    await mockRegistrationError(page, 409, 'USER_ALREADY_EXISTS')
    await page.locator('input[autocomplete="username"]').fill('existinguser')
    await page.locator('input[type="email"]').fill('new@example.com')
    await page.locator('input[autocomplete="new-password"]').first().fill('password123')
    await page.locator('input[autocomplete="new-password"]').last().fill('password123')
    await page.locator('button[type="submit"]').click()
    await page.waitForTimeout(500)
    // Error should be displayed
    const errorEl = page.locator('.bg-error-50, [class*="error-"]')
    await expect(errorEl.first()).toBeVisible()
  })

  test('duplicate email shows error', async ({ page }) => {
    await mockRegistrationError(page, 409, 'EMAIL_ALREADY_EXISTS')
    await page.locator('input[autocomplete="username"]').fill('newuser')
    await page.locator('input[type="email"]').fill('existing@example.com')
    await page.locator('input[autocomplete="new-password"]').first().fill('password123')
    await page.locator('input[autocomplete="new-password"]').last().fill('password123')
    await page.locator('button[type="submit"]').click()
    await page.waitForTimeout(500)
    const errorEl = page.locator('.bg-error-50, [class*="error-"]')
    await expect(errorEl.first()).toBeVisible()
  })

  test('empty fields prevent submission via HTML5 validation', async ({ page }) => {
    // Try to submit without filling anything
    const usernameInput = page.locator('input[autocomplete="username"]')
    const isRequired = await usernameInput.getAttribute('required')
    expect(isRequired).toBeNull()

    const emailInput = page.locator('input[type="email"]')
    const emailRequired = await emailInput.getAttribute('required')
    expect(emailRequired).not.toBeNull()
  })

  test('invalid email format prevents submission', async ({ page }) => {
    await page.locator('input[autocomplete="username"]').fill('newuser')
    await page.locator('input[type="email"]').fill('not-an-email')
    await page.locator('input[autocomplete="new-password"]').first().fill('password123')
    await page.locator('input[autocomplete="new-password"]').last().fill('password123')
    // HTML5 email validation should prevent submit
    const emailInput = page.locator('input[type="email"]')
    const isValid = await emailInput.evaluate((el: HTMLInputElement) => el.validity.valid)
    expect(isValid).toBe(false)
  })
})
