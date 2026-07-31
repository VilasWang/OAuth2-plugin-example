import { Page } from '@playwright/test'

export const MOCK_USER = {
  sub: '6cb8d705-a0a8-4bff-acbd-dc3ce0f610bc',
  name: 'testuser',
  email: 'test@example.com',
  email_verified: true,
  roles: ['user'],
}

export const MOCK_PROFILE = {
  username: 'testuser',
  email: 'test@example.com',
  email_verified: true,
  mfa_enabled: false,
}

export const MOCK_AUTHORIZED_APPS = [
  { client_id: 'third-party-app', name: 'Third Party App', scope: 'openid profile' },
  { client_id: 'mobile-app', name: 'Mobile App', scope: 'openid email' },
]

export async function setupMocks(page: Page) {
  await page.route('**/oauth2/login', async (route) => {
    const body = route.request().postData() || ''
    if (body.includes('password=wrong')) {
      // Post-standardization backend returns the unified Error Envelope; the
      // frontend maps error.code -> localized message via the shared catalog.
      await route.fulfill({ status: 401, contentType: 'application/json', body: JSON.stringify({ error: { code: 'AUTH_INVALID_CREDENTIALS', category: 'AUTHENTICATION', message: '用户名或密码错误', numeric_code: 4001, request_id: 'req-e2e-invalid-credentials' } }) })
    } else {
      await route.fulfill({ status: 200, contentType: 'application/json', body: JSON.stringify({ code: 'mock-auth-code-12345' }) })
    }
  })

  await page.route('**/oauth2/token', async (route) => {
    await route.fulfill({ status: 200, contentType: 'application/json', body: JSON.stringify({ access_token: 'mock-access-token', refresh_token: 'mock-refresh-token', token_type: 'Bearer', expires_in: 3600 }) })
  })

  await page.route('**/oauth2/userinfo', async (route) => {
    await route.fulfill({ status: 200, contentType: 'application/json', body: JSON.stringify(MOCK_USER) })
  })

  await page.route('**/api/register', async (route) => {
    await route.fulfill({ status: 200, contentType: 'application/json', body: JSON.stringify({ message: 'User registered successfully' }) })
  })

  await page.route('**/api/password-reset/request', async (route) => {
    await route.fulfill({ status: 200, contentType: 'application/json', body: JSON.stringify({ message: 'If the email exists, a reset link has been sent' }) })
  })

  await page.route('**/api/password-reset/confirm', async (route) => {
    await route.fulfill({ status: 200, contentType: 'application/json', body: JSON.stringify({ message: 'Password reset successfully' }) })
  })

  await page.route('**/api/verify-email**', async (route) => {
    if (route.request().method() === 'GET') {
      await route.fulfill({ status: 200, contentType: 'application/json', body: JSON.stringify({ message: 'Email verified successfully' }) })
    } else {
      await route.continue()
    }
  })

  await page.route('**/api/verify-email/resend', async (route) => {
    await route.fulfill({ status: 200, contentType: 'application/json', body: JSON.stringify({ message: 'Verification email sent' }) })
  })

  await page.route('**/api/me', async (route) => {
    if (route.request().method() === 'GET') {
      await route.fulfill({ status: 200, contentType: 'application/json', body: JSON.stringify(MOCK_PROFILE) })
    } else if (route.request().method() === 'DELETE') {
      await route.fulfill({ status: 200, contentType: 'application/json', body: JSON.stringify({ message: 'Account deleted' }) })
    } else { await route.continue() }
  })

  await page.route('**/api/me/password', async (route) => {
    await route.fulfill({ status: 200, contentType: 'application/json', body: JSON.stringify({ message: 'Password changed successfully' }) })
  })

  await page.route('**/api/me/mfa/setup', async (route) => {
    await route.fulfill({ status: 200, contentType: 'application/json', body: JSON.stringify({ secret: 'JBSWY3DPEHPK3PXP', qr_uri: 'otpauth://totp/OAuth2:testuser?secret=JBSWY3DPEHPK3PXP' }) })
  })

  await page.route('**/api/me/mfa/verify', async (route) => {
    await route.fulfill({ status: 200, contentType: 'application/json', body: JSON.stringify({ message: 'MFA enabled' }) })
  })

  await page.route('**/api/me/mfa/disable', async (route) => {
    await route.fulfill({ status: 200, contentType: 'application/json', body: JSON.stringify({ message: 'MFA disabled' }) })
  })

  await page.route('**/api/me/authorized-apps', async (route) => {
    await route.fulfill({ status: 200, contentType: 'application/json', body: JSON.stringify({ apps: MOCK_AUTHORIZED_APPS }) })
  })

  await page.route('**/api/me/authorized-apps/*', async (route) => {
    if (route.request().method() === 'DELETE') {
      await route.fulfill({ status: 200, contentType: 'application/json', body: JSON.stringify({ message: 'Authorization revoked' }) })
    } else { await route.continue() }
  })

  // WebAuthn credentials
  await page.route('**/api/me/webauthn/credentials', async (route) => {
    await route.fulfill({ status: 200, contentType: 'application/json', body: JSON.stringify({ credentials: [{ id: 'cred-1', name: 'My Passkey', created_at: '2026-05-20T00:00:00Z' }] }) })
  })

  await page.route('**/oauth2/consent', async (route) => {
    await route.fulfill({ status: 200, contentType: 'application/json', body: JSON.stringify({ redirect_uri: 'http://localhost:5173/callback?code=consent-code&state=test' }) })
  })

  await page.route('**/oauth2/device/verify', async (route) => {
    await route.fulfill({ status: 200, contentType: 'application/json', body: JSON.stringify({ message: 'Device authorized' }) })
  })

  await page.route('**/oauth2/revoke', async (route) => {
    await route.fulfill({ status: 200, contentType: 'application/json', body: JSON.stringify({}) })
  })

  await page.route('**/oauth2/mfa/verify', async (route) => {
    await route.fulfill({ status: 200, contentType: 'application/json', body: JSON.stringify({ access_token: 'mock-mfa-token', refresh_token: 'mock-mfa-refresh', token_type: 'Bearer' }) })
  })
}

export async function loginUser(page: Page) {
  // Set tokens in localStorage before navigating
  await page.addInitScript(() => {
    localStorage.setItem('access_token', 'mock-access-token')
    localStorage.setItem('refresh_token', 'mock-refresh-token')
  })
  await page.goto('/')
  // Wait for the page to load (should not redirect to /login since token is set)
  await page.waitForLoadState('networkidle')
}

export async function loginViaForm(page: Page) {
  await page.goto('/login')
  await page.locator('input[autocomplete="username"]').fill('testuser')
  await page.locator('input[autocomplete="current-password"]').fill('password123')
  await page.locator('button[type="submit"]').click()
  await page.waitForTimeout(2000)
}

/**
 * Override a previously registered route with a custom handler.
 */
export async function overrideRoute(page: Page, urlPattern: string, handler: (route: any) => Promise<void>) {
  await page.route(urlPattern, handler)
}

/**
 * Mock a specific API returning an error status with Error Envelope body.
 */
export async function mockApiError(page: Page, urlPattern: string, status: number, body: object) {
  await page.route(urlPattern, async (route) => {
    await route.fulfill({
      status,
      contentType: 'application/json',
      body: JSON.stringify(body),
    })
  })
}

/**
 * Mock a network failure for a given URL pattern.
 */
export async function mockNetworkError(page: Page, urlPattern: string) {
  await page.route(urlPattern, async (route) => {
    await route.abort('failed')
  })
}

/**
 * Mock registration API to return an error (e.g., duplicate user).
 */
export async function mockRegistrationError(page: Page, status: number, errorCode: string) {
  await page.route('**/api/register', async (route) => {
    await route.fulfill({
      status,
      contentType: 'application/json',
      body: JSON.stringify({
        error: { code: errorCode, category: 'VALIDATION', message: 'User already exists' },
      }),
    })
  })
}

/**
 * Mock password change API to return an error.
 */
export async function mockPasswordChangeError(page: Page, errorCode: string) {
  await page.route('**/api/me/password', async (route) => {
    await route.fulfill({
      status: 400,
      contentType: 'application/json',
      body: JSON.stringify({
        error: { code: errorCode, category: 'AUTHENTICATION', message: 'Wrong password' },
      }),
    })
  })
}
