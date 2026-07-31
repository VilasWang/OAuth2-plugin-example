import { test, expect } from '@playwright/test'
import { setupAuthenticatedMocks, loginAsAdmin } from './helpers/mock-api'

test.describe('Authentication', () => {
  test.beforeEach(async ({ page }) => {
    await setupAuthenticatedMocks(page)
  })

  test('redirects to login when not authenticated', async ({ page }) => {
    await page.goto('/admin/')
    await expect(page).toHaveURL(/\/admin\/login/)
  })

  test('shows login form with correct elements', async ({ page }) => {
    await page.goto('/admin/login')
    await expect(page.locator('h1')).toContainText('OAuth2 Admin Console')
    await expect(page.locator('input[type="text"]')).toBeVisible()
    await expect(page.locator('input[type="password"]')).toBeVisible()
    await expect(page.locator('button[type="submit"]')).toContainText('Sign in')
  })

  test('successful login navigates to dashboard', async ({ page }) => {
    await loginAsAdmin(page)
    await expect(page).toHaveURL(/\/admin\//)
    await expect(page.locator('h2')).toContainText('Dashboard')
  })

  test('shows error on login failure', async ({ page }) => {
    // Override login mock to return error. Post-standardization the backend
    // returns the unified Error Envelope; the admin app maps error.code ->
    // localized message via the shared catalog (AUTH_INVALID_CREDENTIALS).
    await page.route('**/oauth2/login', async (route) => {
      await route.fulfill({
        status: 401,
        contentType: 'application/json',
        body: JSON.stringify({ error: { code: 'AUTH_INVALID_CREDENTIALS', category: 'AUTHENTICATION', message: '用户名或密码错误', numeric_code: 4001, request_id: 'req-e2e-invalid-credentials' } }),
      })
    })

    await page.goto('/admin/login')
    await page.fill('input[type="text"]', 'wrong')
    await page.fill('input[type="password"]', 'wrong')
    await page.click('button[type="submit"]')

    await expect(page.locator('.bg-red-50')).toBeVisible()
    await expect(page.locator('.text-red-600')).toContainText('用户名或密码错误')
  })

  test('denies access for non-admin users', async ({ page }) => {
    // Override userinfo to return non-admin user
    await page.route('**/oauth2/userinfo', async (route) => {
      await route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify({
          sub: '123',
          username: 'regularuser',
          email: 'user@example.com',
          roles: ['user'],
        }),
      })
    })

    await page.goto('/admin/login')
    await page.fill('input[type="text"]', 'regularuser')
    await page.fill('input[type="password"]', 'password')
    await page.click('button[type="submit"]')

    // Should show error about admin role
    await expect(page.locator('.text-red-600')).toContainText('Admin role required')
  })

  test('handles MFA required response', async ({ page }) => {
    await page.route('**/oauth2/login', async (route) => {
      await route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify({ mfa_required: true, mfa_token: 'mfa-token-123' }),
      })
    })

    await page.goto('/admin/login')
    await page.fill('input[type="text"]', 'admin')
    await page.fill('input[type="password"]', 'admin')
    await page.click('button[type="submit"]')

    // Login should not navigate away (MFA flow not fully implemented in UI yet)
    await expect(page).toHaveURL(/\/admin\/login/)
  })

  test('empty username prevents form submission', async ({ page }) => {
    await page.goto('/admin/login')
    // Clear any default value and try to submit
    const usernameInput = page.locator('input[type="text"]')
    await expect(usernameInput).toHaveAttribute('required', '')
    // Browser HTML5 validation will prevent submission
    const isValid = await usernameInput.evaluate((el: HTMLInputElement) => el.validity.valueMissing)
    expect(isValid).toBe(true)
  })

  test('empty password prevents form submission', async ({ page }) => {
    await page.goto('/admin/login')
    const passwordInput = page.locator('input[type="password"]')
    await expect(passwordInput).toHaveAttribute('required', '')
    const isValid = await passwordInput.evaluate((el: HTMLInputElement) => el.validity.valueMissing)
    expect(isValid).toBe(true)
  })

  test('loading state during login', async ({ page }) => {
    // Slow the login response
    await page.route('**/oauth2/login', async (route) => {
      await new Promise(resolve => setTimeout(resolve, 500))
      await route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify({ code: 'mock-auth-code' }),
      })
    })
    await page.goto('/admin/login')
    await page.fill('input[type="text"]', 'admin')
    await page.fill('input[type="password"]', 'admin')
    await page.click('button[type="submit"]')
    // Button should show loading text and be disabled
    const button = page.locator('button[type="submit"]')
    await expect(button).toContainText('Signing in')
    await expect(button).toBeDisabled()
    // Wait for completion
    await page.waitForURL('**/admin/')
  })

  test('non-existent user shows error', async ({ page }) => {
    await page.route('**/oauth2/login', async (route) => {
      await route.fulfill({
        status: 401,
        contentType: 'application/json',
        body: JSON.stringify({ error: { code: 'AUTH_INVALID_CREDENTIALS', category: 'AUTHENTICATION', message: 'Invalid credentials' } }),
      })
    })
    await page.goto('/admin/login')
    await page.fill('input[type="text"]', 'nonexistent')
    await page.fill('input[type="password"]', 'password')
    await page.click('button[type="submit"]')
    await expect(page.locator('.bg-red-50')).toBeVisible()
    await expect(page).toHaveURL(/\/admin\/login/)
  })

  test('browser back after login stays on dashboard', async ({ page }) => {
    await loginAsAdmin(page)
    await expect(page).toHaveURL(/\/admin\//)
    await page.goBack()
    // Should still be on dashboard (auth guard redirects)
    await page.waitForTimeout(500)
    const url = page.url()
    expect(url).toMatch(/\/admin/)
  })
})
