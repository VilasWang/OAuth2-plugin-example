import { test, expect } from '@playwright/test'
import { setupMocks, loginUser } from './helpers/mock-api'

test.describe('OAuth2 Consent Page', () => {
  test.beforeEach(async ({ page }) => {
    await setupMocks(page)
    await loginUser(page)
  })

  test('displays consent page with client info', async ({ page }) => {
    await page.goto('/consent?client_id=third-party&scope=openid+profile+email&redirect_uri=http://example.com/callback&state=test123')
    await expect(page.getByRole('heading', { name: /authorize/i })).toBeVisible()
    await expect(page.locator('text=third-party')).toBeVisible()
  })

  test('shows requested scopes', async ({ page }) => {
    await page.goto('/consent?client_id=third-party&scope=openid+profile+email&redirect_uri=http://example.com/callback&state=test123')
    await expect(page.locator('text=Verify your identity')).toBeVisible()
    await expect(page.locator('text=Access your basic profile')).toBeVisible()
    await expect(page.locator('text=Access your email')).toBeVisible()
  })

  test('has approve and deny buttons', async ({ page }) => {
    await page.goto('/consent?client_id=third-party&scope=openid&redirect_uri=http://example.com/callback&state=test')
    await expect(page.locator('button:has-text("Authorize")')).toBeVisible()
    await expect(page.locator('button:has-text("Deny")')).toBeVisible()
  })
})

test.describe('Device Verification Page', () => {
  test.beforeEach(async ({ page }) => {
    await setupMocks(page)
  })

  test('displays device code input', async ({ page }) => {
    await page.goto('/device/verify')
    await expect(page.getByRole('heading', { name: /device/i })).toBeVisible()
    await expect(page.locator('input')).toBeVisible()
  })

  test('pre-fills code from URL', async ({ page }) => {
    await page.goto('/device/verify?user_code=ABCD-EFGH')
    await expect(page.locator('input')).toHaveValue('ABCD-EFGH')
  })

  test('shows success after verification', async ({ page }) => {
    await page.goto('/device/verify')
    await page.locator('input').fill('ABCD-EFGH')
    await page.click('button:has-text("Authorize Device")')
    await expect(page.locator('text=authorized successfully')).toBeVisible()
  })
})

test.describe('Callback Page', () => {
  test.beforeEach(async ({ page }) => {
    await setupMocks(page)
  })

  test('exchanges code for token and redirects', async ({ page }) => {
    await page.goto('/callback?code=test-auth-code&state=test-state')
    // After successful token exchange, should redirect to /
    await expect(page).toHaveURL('/', { timeout: 10000 })
  })

  test('shows error when error param present', async ({ page }) => {
    await page.goto('/callback?error=access_denied&error_description=User+denied+access')
    await expect(page.locator('text=User denied access')).toBeVisible()
  })

  test('shows error when no code present', async ({ page }) => {
    await page.goto('/callback')
    await expect(page.locator('text=No authorization code')).toBeVisible()
  })

  test('loading spinner shown while exchanging code', async ({ page }) => {
    // Delay token exchange to observe spinner
    await page.route('**/oauth2/token', async (route) => {
      await new Promise(resolve => setTimeout(resolve, 800))
      await route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify({ access_token: 'slow-token', refresh_token: 'slow-refresh', token_type: 'Bearer', expires_in: 3600 }),
      })
    })
    await page.goto('/callback?code=valid-code&state=test-state')
    // Spinner should be visible during exchange
    await expect(page.locator('.animate-spin')).toBeVisible({ timeout: 3000 })
    // After exchange completes, redirect happens
    await page.waitForTimeout(1500)
    const url = page.url()
    expect(url).not.toContain('/callback')
  })
})
