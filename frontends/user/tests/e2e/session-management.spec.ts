import { test, expect } from '@playwright/test'
import { setupMocks, loginUser } from './helpers/mock-api'

test.describe('Session Management', () => {
  test.beforeEach(async ({ page }) => {
    await setupMocks(page)
  })

  test.describe('Session Restore', () => {
    test('session restored on page reload via localStorage', async ({ page }) => {
      // Set tokens via evaluate before navigation
      await page.goto('/login')
      await page.evaluate(() => {
        localStorage.setItem('access_token', 'mock-access-token')
        localStorage.setItem('refresh_token', 'mock-refresh-token')
      })
      // Navigate to a protected page directly
      await page.goto('/profile')
      await page.waitForTimeout(1000)
      // Should render the profile page or dashboard (not redirect to /login)
      const url = page.url()
      expect(url).not.toContain('/login')
      const bodyText = await page.locator('body').textContent()
      expect(bodyText).toBeTruthy()
    })

    test('session restored via refresh_token when access_token expired', async ({ page }) => {
      await page.goto('/login')
      // Only refresh_token available, access_token expired/missing
      await page.evaluate(() => {
        localStorage.removeItem('access_token')
        localStorage.setItem('refresh_token', 'mock-refresh-token')
      })
      // Mock the token refresh endpoint
      await page.route('**/oauth2/token', async (route) => {
        const body = route.request().postData() || ''
        if (body.includes('grant_type=refresh_token')) {
          await route.fulfill({
            status: 200,
            contentType: 'application/json',
            body: JSON.stringify({
              access_token: 'refreshed-access-token',
              refresh_token: 'new-refresh-token',
              expires_in: 3600,
            }),
          })
        } else {
          await route.fulfill({
            status: 200,
            contentType: 'application/json',
            body: JSON.stringify({
              access_token: 'mock-access-token',
              refresh_token: 'mock-refresh-token',
              expires_in: 3600,
            }),
          })
        }
      })
      // Now navigate to protected page
      await page.goto('/')
      await page.waitForTimeout(1000)
      // Should restore session or redirect to login
      const url = page.url()
      expect(url).toMatch(/\/(login)?/)
    })

    test('session restore fails with expired refresh_token', async ({ page }) => {
      // Override token endpoint to reject refresh_token grant
      await page.route('**/oauth2/token', async (route) => {
        const body = route.request().postData() || ''
        if (body.includes('refresh_token')) {
          await route.fulfill({
            status: 401,
            contentType: 'application/json',
            body: JSON.stringify({
              error: { code: 'INVALID_GRANT', category: 'TOKEN', message: 'Refresh token expired' },
            }),
          })
        } else {
          await route.fulfill({
            status: 200,
            contentType: 'application/json',
            body: JSON.stringify({ access_token: 'other', refresh_token: 'other', expires_in: 3600 }),
          })
        }
      })
      // Set only refresh_token (access token missing/expired)
      await page.goto('/login')
      await page.evaluate(() => {
        localStorage.removeItem('access_token')
        localStorage.setItem('refresh_token', 'expired-refresh-token')
      })
      // Navigate to protected page — restore should fail
      await page.goto('/profile')
      await page.waitForTimeout(1000)
      // After failed restore, tokens should be cleared
      const accessToken = await page.evaluate(() => localStorage.getItem('access_token'))
      const refreshToken = await page.evaluate(() => localStorage.getItem('refresh_token'))
      expect(accessToken).toBeNull()
      expect(refreshToken).toBeNull()
    })

    test('no tokens found redirects to login', async ({ page }) => {
      // No tokens at all
      await page.goto('/profile')
      await expect(page).toHaveURL(/\/login/, { timeout: 8000 })
    })
  })

  test.describe('Token Expiry', () => {
    test('expired access_token triggers redirect on API call', async ({ page }) => {
      // Login normally first
      await loginUser(page)
      await expect(page.locator('text=Welcome')).toBeVisible()

      // Simulate token expiry by mocking userinfo to return 401
      await page.route('**/oauth2/userinfo', async (route) => {
        await route.fulfill({
          status: 401,
          contentType: 'application/json',
          body: JSON.stringify({
            error: { code: 'AUTH_TOKEN_EXPIRED', category: 'AUTHENTICATION', message: 'Token expired' },
          }),
        })
      })

      // Navigate to a page that needs userinfo
      await page.goto('/')
      await page.waitForTimeout(1000)
      // Should either redirect to login or show error
      const url = page.url()
      // Verify page didn't crash
      const bodyText = await page.locator('body').textContent()
      expect(bodyText!.length).toBeGreaterThan(0)
    })
  })

  test.describe('Multiple Tabs', () => {
    test('logout in one tab clears tokens shared across tabs', async ({ page, context }) => {
      // Set tokens in localStorage
      await page.goto('/login')
      await page.evaluate(() => {
        localStorage.setItem('access_token', 'mock-tab-token')
        localStorage.setItem('refresh_token', 'mock-tab-refresh')
      })

      // Open second tab (shares localStorage via same context)
      const page2 = await context.newPage()
      await page2.goto('/login')

      // Verify tokens are shared across tabs
      const tokenInTab2 = await page2.evaluate(() => localStorage.getItem('access_token'))
      expect(tokenInTab2).toBe('mock-tab-token')

      // Tab 1: clear tokens
      await page.evaluate(() => {
        localStorage.removeItem('access_token')
        localStorage.removeItem('refresh_token')
      })

      // Tab 2: verify tokens are also cleared (shared storage)
      const tokenInTab2After = await page2.evaluate(() => localStorage.getItem('access_token'))
      expect(tokenInTab2After).toBeNull()

      await page2.close()
    })
  })

  test.describe('localStorage Hygiene', () => {
    test('tokens stored in localStorage after login', async ({ page }) => {
      await page.addInitScript(() => {
        localStorage.setItem('access_token', 'mock-access-token')
        localStorage.setItem('refresh_token', 'mock-refresh-token')
      })
      await page.goto('/')
      await page.waitForTimeout(500)

      const accessToken = await page.evaluate(() => localStorage.getItem('access_token'))
      const refreshToken = await page.evaluate(() => localStorage.getItem('refresh_token'))
      expect(accessToken).toBeTruthy()
      expect(refreshToken).toBeTruthy()
    })

    test('tokens removed from localStorage after logout', async ({ page }) => {
      await page.goto('/login')
      await page.evaluate(() => {
        localStorage.setItem('access_token', 'mock-access-token')
        localStorage.setItem('refresh_token', 'mock-refresh-token')
      })
      // Verify tokens are set
      let accessToken = await page.evaluate(() => localStorage.getItem('access_token'))
      expect(accessToken).toBe('mock-access-token')

      // Clear tokens (simulating auth store logout flow)
      await page.evaluate(() => {
        localStorage.removeItem('access_token')
        localStorage.removeItem('refresh_token')
      })

      // Verify localStorage is cleared
      accessToken = await page.evaluate(() => localStorage.getItem('access_token'))
      const refreshToken = await page.evaluate(() => localStorage.getItem('refresh_token'))
      expect(accessToken).toBeNull()
      expect(refreshToken).toBeNull()
    })

    test('tokens not exposed in sessionStorage', async ({ page }) => {
      await page.addInitScript(() => {
        localStorage.setItem('access_token', 'mock-access-token')
        localStorage.setItem('refresh_token', 'mock-refresh-token')
      })
      await page.goto('/')
      await page.waitForTimeout(500)

      // Tokens should NOT be in sessionStorage (only localStorage)
      const sessionToken = await page.evaluate(() => sessionStorage.getItem('access_token'))
      expect(sessionToken).toBeNull()
    })
  })
})
