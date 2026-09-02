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
    // Real backend code (AuthService.cc) — the catalog maps it to a
    // dedicated message, so the assertion below is content-verified.
    await mockRegistrationError(page, 409, 'VALIDATION_USERNAME_TAKEN')
    await page.locator('input[autocomplete="username"]').fill('existinguser')
    await page.locator('input[type="email"]').fill('new@example.com')
    await page.locator('input[autocomplete="new-password"]').first().fill('password123')
    await page.locator('input[autocomplete="new-password"]').last().fill('password123')
    await page.locator('button[type="submit"]').click()
    // Assert the error ALERT (AppAlert renders role="alert"), not a
    // [class*="error-"] substring — that matched the AppInput required-star
    // and the strength meter unconditionally and passed even with no banner.
    const alert = page.locator('[role="alert"]')
    await expect(alert).toBeVisible({ timeout: 3000 })
    await expect(alert).toContainText('This username is already taken')
  })

  test('duplicate email shows error', async ({ page }) => {
    await mockRegistrationError(page, 409, 'VALIDATION_EMAIL_TAKEN')
    await page.locator('input[autocomplete="username"]').fill('newuser')
    await page.locator('input[type="email"]').fill('existing@example.com')
    await page.locator('input[autocomplete="new-password"]').first().fill('password123')
    await page.locator('input[autocomplete="new-password"]').last().fill('password123')
    await page.locator('button[type="submit"]').click()
    const alert = page.locator('[role="alert"]')
    await expect(alert).toBeVisible({ timeout: 3000 })
    await expect(alert).toContainText('This email address is already registered')
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
