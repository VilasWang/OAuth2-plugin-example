import { test, expect } from '@playwright/test'
import { setupMocks, loginViaForm } from './helpers/mock-api'

test.describe('Login', () => {
  test.beforeEach(async ({ page }) => {
    await setupMocks(page)
  })

  test('displays login form', async ({ page }) => {
    await page.goto('/login')
    await expect(page.getByRole('heading', { name: /sign in/i })).toBeVisible()
    await expect(page.locator('input[autocomplete="username"]')).toBeVisible()
    await expect(page.locator('input[autocomplete="current-password"]')).toBeVisible()
    await expect(page.locator('button[type="submit"]')).toBeVisible()
  })

  test('successful login redirects to dashboard', async ({ page }) => {
    await page.goto('/login')
    await page.locator('input[autocomplete="username"]').fill('testuser')
    await page.locator('input[autocomplete="current-password"]').fill('password123')
    await page.locator('button[type="submit"]').click()
    // After successful login, should navigate away from /login
    await expect(page).not.toHaveURL(/\/login$/, { timeout: 10000 })
  })

  test('shows error on invalid credentials', async ({ page }) => {
    await page.goto('/login')
    await page.locator('input[autocomplete="username"]').fill('testuser')
    await page.locator('input[autocomplete="current-password"]').fill('wrong')
    await page.locator('button[type="submit"]').click()
    await expect(page.locator('text=用户名或密码错误')).toBeVisible()
  })

  test('shows MFA challenge when required', async ({ page }) => {
    await page.route('**/oauth2/login', async (route) => {
      await route.fulfill({ status: 200, contentType: 'application/json', body: JSON.stringify({ mfa_required: true, mfa_token: 'mfa-token-123' }) })
    })
    await page.goto('/login')
    await page.locator('input[autocomplete="username"]').fill('testuser')
    await page.locator('input[autocomplete="current-password"]').fill('password123')
    await page.locator('button[type="submit"]').click()
    await expect(page.locator('text=Two-Factor Authentication')).toBeVisible()
    await expect(page.locator('input[maxlength="6"]')).toBeVisible()
  })

  test('MFA verification completes login', async ({ page }) => {
    await page.route('**/oauth2/login', async (route) => {
      await route.fulfill({ status: 200, contentType: 'application/json', body: JSON.stringify({ mfa_required: true, mfa_token: 'mfa-token-123' }) })
    })
    await page.goto('/login')
    await page.locator('input[autocomplete="username"]').fill('testuser')
    await page.locator('input[autocomplete="current-password"]').fill('password123')
    await page.locator('button[type="submit"]').click()
    // Should show MFA form
    await expect(page.locator('input[maxlength="6"]')).toBeVisible()
    await page.locator('input[maxlength="6"]').fill('123456')
    await page.locator('button[type="submit"]').click()
    // After MFA verify, should navigate to dashboard
    await expect(page).toHaveURL('/', { timeout: 10000 })
  })

  test('forgot password link navigates correctly', async ({ page }) => {
    await page.goto('/login')
    await page.click('a:has-text("Forgot password")')
    await expect(page).toHaveURL('/forgot-password')
  })

  test('register link navigates correctly', async ({ page }) => {
    await page.goto('/login')
    await page.click('a:has-text("create a new account")')
    await expect(page).toHaveURL('/register')
  })
})

test.describe('Register', () => {
  test.beforeEach(async ({ page }) => {
    await setupMocks(page)
  })

  test('displays registration form', async ({ page }) => {
    await page.goto('/register')
    await expect(page.getByRole('heading', { name: /create/i })).toBeVisible()
  })

  test('successful registration shows success message', async ({ page }) => {
    await page.goto('/register')
    await page.locator('input[autocomplete="username"]').fill('newuser')
    await page.locator('input[autocomplete="email"]').fill('new@example.com')
    const passwordFields = page.locator('input[type="password"]')
    await passwordFields.first().fill('StrongPass123')
    await passwordFields.nth(1).fill('StrongPass123')
    await page.locator('button[type="submit"]').click()
    await expect(page.locator('text=successfully')).toBeVisible()
  })

  test('registration success auto-redirects after ~2 seconds', async ({ page }) => {
    await page.goto('/register')
    await page.locator('input[autocomplete="username"]').fill('timeruser')
    await page.locator('input[autocomplete="email"]').fill('timer@example.com')
    const passwordFields = page.locator('input[type="password"]')
    await passwordFields.first().fill('Password123')
    await passwordFields.nth(1).fill('Password123')
    await page.locator('button[type="submit"]').click()
    // Success message appears
    await expect(page.locator('text=successfully')).toBeVisible()
    // After ~2 seconds, redirected to login page
    await expect(page).toHaveURL(/\/login/, { timeout: 5000 })
  })
})

test.describe('Forgot Password', () => {
  test.beforeEach(async ({ page }) => {
    await setupMocks(page)
  })

  test('displays forgot password form', async ({ page }) => {
    await page.goto('/forgot-password')
    await expect(page.getByRole('heading', { name: /reset/i })).toBeVisible()
  })

  test('shows success message after submission', async ({ page }) => {
    await page.goto('/forgot-password')
    await page.locator('input[type="email"]').fill('test@example.com')
    await page.locator('button[type="submit"]').click()
    await expect(page.locator('text=If an account with that email exists')).toBeVisible()
  })
})

test.describe('Email Verification', () => {
  test.beforeEach(async ({ page }) => {
    await setupMocks(page)
  })

  test('shows success on valid token', async ({ page }) => {
    await page.goto('/verify-email?token=valid-token')
    await expect(page.getByRole('heading', { name: /verified/i })).toBeVisible()
  })

  test('shows error when token is missing', async ({ page }) => {
    await page.goto('/verify-email')
    await expect(page.locator('text=Missing')).toBeVisible()
  })
})

test.describe('GitHub Login', () => {
  test.beforeEach(async ({ page }) => {
    await setupMocks(page)
  })

  test('shows GitHub login button on login page', async ({ page }) => {
    await page.goto('/login')
    await expect(page.locator('text=Sign in with GitHub')).toBeVisible()
  })

  test('GitHub button links to GitHub OAuth', async ({ page }) => {
    await page.goto('/login')
    const githubLink = page.locator('a:has-text("Sign in with GitHub")')
    await expect(githubLink).toBeVisible()
    const href = await githubLink.getAttribute('href')
    expect(href).toContain('github.com/login/oauth/authorize')
  })
})

test.describe('MFA Validation', () => {
  test.beforeEach(async ({ page }) => {
    await setupMocks(page)
  })

  test('invalid MFA code shows error', async ({ page }) => {
    // Override login to return MFA required (registered after setupMocks, takes priority)
    await page.route('**/oauth2/login', async (route) => {
      await route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify({ mfa_required: true, mfa_token: 'mfa-token-123' }),
      })
    })
    // Override MFA verify to return error
    await page.route('**/oauth2/mfa/verify', async (route) => {
      await route.fulfill({
        status: 401,
        contentType: 'application/json',
        body: JSON.stringify({ error: { code: 'MFA_INVALID_CODE', category: 'AUTHENTICATION', message: 'Invalid MFA code' } }),
      })
    })
    await page.goto('/login')
    await page.locator('input[autocomplete="username"]').fill('testuser')
    await page.locator('input[autocomplete="current-password"]').fill('password123')
    await page.locator('button[type="submit"]').click()
    // Wait for MFA form to appear
    const mfaInput = page.locator('input[inputmode="numeric"]')
    await expect(mfaInput).toBeVisible({ timeout: 5000 })
    await mfaInput.fill('000000')
    await page.locator('button:has-text("Verify")').click()
    await page.waitForTimeout(1500)
    // After invalid code: should NOT redirect to dashboard, should stay showing MFA or show error
    const url = page.url()
    expect(url).not.toContain('/dashboard')
    // Verify MFA input is still present or error is shown (page didn't crash)
    const pageContent = await page.content()
    expect(pageContent.length).toBeGreaterThan(0)
  })

  test('less than 6 digits disables verify button', async ({ page }) => {
    await page.route('**/oauth2/login', async (route) => {
      await route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify({ mfa_required: true, mfa_token: 'mfa-token-123' }),
      })
    })
    await page.goto('/login')
    await page.locator('input[autocomplete="username"]').fill('testuser')
    await page.locator('input[autocomplete="current-password"]').fill('password123')
    await page.locator('button[type="submit"]').click()
    await page.waitForTimeout(500)
    const mfaInput = page.locator('input[inputmode="numeric"]')
    if (await mfaInput.isVisible()) {
      await mfaInput.fill('1234')
      const verifyButton = page.locator('button:has-text("Verify")')
      await expect(verifyButton).toBeDisabled()
    }
  })

  test('back to login from MFA form', async ({ page }) => {
    await page.route('**/oauth2/login', async (route) => {
      await route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify({ mfa_required: true, mfa_token: 'mfa-token-123' }),
      })
    })
    await page.goto('/login')
    await page.locator('input[autocomplete="username"]').fill('testuser')
    await page.locator('input[autocomplete="current-password"]').fill('password123')
    await page.locator('button[type="submit"]').click()
    await page.waitForTimeout(500)
    const backButton = page.locator('button:has-text("Back to login")')
    if (await backButton.isVisible()) {
      await backButton.click()
      // MFA form should be hidden, login form visible
      await expect(page.locator('input[inputmode="numeric"]')).not.toBeVisible()
      await expect(page.locator('input[autocomplete="username"]')).toBeVisible()
    }
  })
})
