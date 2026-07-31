import { test, expect } from '@playwright/test'
import { setupAuthenticatedMocks, loginAsAdmin, overrideRoute } from './helpers/mock-api'

test.describe('Security', () => {
  test.describe('Injection & XSS on Login', () => {
    test('SQL injection in login username is rejected', async ({ page }) => {
      await setupAuthenticatedMocks(page)
      // Override login to always reject this credential
      await overrideRoute(page, '**/oauth2/login', async (route) => {
        await route.fulfill({
          status: 401,
          contentType: 'application/json',
          body: JSON.stringify({
            error: { code: 'AUTH_INVALID_CREDENTIALS', category: 'AUTHENTICATION', message: 'Invalid credentials' },
          }),
        })
      })
      await page.goto('/admin/login')
      await page.fill('input[type="text"]', "' OR 1=1 --")
      await page.fill('input[type="password"]', 'anything')
      await page.click('button[type="submit"]')
      await page.waitForTimeout(500)
      // Should stay on login page with error
      await expect(page).toHaveURL(/\/admin\/login/)
      const errorEl = page.locator('.bg-red-50, .text-red-600')
      await expect(errorEl.first()).toBeVisible()
    })

    test('XSS in login username is rendered as text', async ({ page }) => {
      await setupAuthenticatedMocks(page)
      await overrideRoute(page, '**/oauth2/login', async (route) => {
        await route.fulfill({
          status: 401,
          contentType: 'application/json',
          body: JSON.stringify({
            error: { code: 'AUTH_INVALID_CREDENTIALS', category: 'AUTHENTICATION', message: 'Invalid credentials' },
          }),
        })
      })
      await page.goto('/admin/login')
      const xssPayload = "<script>alert('xss')</script>"
      await page.fill('input[type="text"]', xssPayload)
      await page.fill('input[type="password"]', 'test')
      await page.click('button[type="submit"]')
      await page.waitForTimeout(500)
      // Should stay on login, no script execution
      await expect(page).toHaveURL(/\/admin\/login/)
      const pageContent = await page.content()
      expect(pageContent).not.toContain('<script>alert')
    })
  })

  test.describe('XSS in CRUD Operations', () => {
    test.beforeEach(async ({ page }) => {
      await setupAuthenticatedMocks(page)
      await loginAsAdmin(page)
    })

    test('XSS in role name is rendered as text', async ({ page }) => {
      await page.click('nav a:has-text("Roles")')
      await page.waitForLoadState('networkidle')

      await page.click('button:has-text("+ Create Role")')
      const xssPayload = '<img onerror="alert(1)" src=x>'
      await page.fill('input[placeholder="e.g. editor"]', xssPayload)
      await page.locator('.fixed button:has-text("Create")').click()
      await page.waitForTimeout(500)

      // Verify the role name is HTML-escaped in the table
      const pageContent = await page.content()
      expect(pageContent).toContain('&lt;img')
    })

    test('XSS in scope name is rendered as text', async ({ page }) => {
      await page.click('nav a:has-text("Scopes")')
      await page.waitForLoadState('networkidle')

      await page.click('button:has-text("+ Create Scope")')
      const xssPayload = '<script>document.cookie</script>'
      await page.fill('input[placeholder="e.g. reports:read"]', xssPayload)
      await page.locator('.fixed button:has-text("Create")').click()
      await page.waitForTimeout(500)

      const pageContent = await page.content()
      expect(pageContent).toContain('&lt;script&gt;')
    })
  })

  test.describe('Secret Exposure', () => {
    test('client secret not in DOM after modal close', async ({ page }) => {
      await setupAuthenticatedMocks(page)
      await loginAsAdmin(page)

      await page.click('nav a:has-text("Applications")')
      await page.waitForLoadState('networkidle')

      // Create a new client to get a secret
      await page.click('button:has-text("Create Application")')
      await page.fill('input[placeholder="My App"]', 'Secret Test App')
      await page.locator('.fixed button[type="submit"]').click()
      await page.waitForTimeout(500)

      // Secret modal should appear — close it
      await page.click('button:has-text("Done")')
      await page.waitForTimeout(300)

      // Verify secret is no longer in the page DOM
      const pageContent = await page.content()
      expect(pageContent).not.toContain('generated-secret-abc123xyz')
      expect(pageContent).not.toContain('new-secret-after-reset')
    })
  })

  // A-SEC-001: This SPA uses Bearer tokens (Authorization header), not cookies,
  // for authentication. Classic CSRF exploits the browser's automatic cookie
  // attachment — irrelevant here because the auth credential is never in a
  // cookie. Cross-origin protection is enforced server-side by a strict
  // CORS Origin allowlist (main.cc, exact match, no wildcards). These tests
  // verify the CSRF-immune invariants the frontend must maintain.
  test.describe('CSRF protection (Bearer-token + CORS)', () => {
    test('auth credential is not stored in a cookie', async ({ page }) => {
      await setupAuthenticatedMocks(page)
      await loginAsAdmin(page)
      // No auth cookie is set — the credential lives in sessionStorage
      // (refresh token) + memory (access token), immune to automatic
      // cross-origin submission.
      const cookieBlob = await page.evaluate(() => document.cookie)
      expect(cookieBlob).toBe('')
    })

    test('API requests carry Bearer token in Authorization header', async ({ page }) => {
      await setupAuthenticatedMocks(page)
      await loginAsAdmin(page)
      // Re-trigger an API call and capture its Authorization header. The
      // axios request interceptor must attach the access token as Bearer.
      const [request] = await Promise.all([
        page.waitForRequest((req) => req.url().includes('/api/admin/dashboard/stats')),
        page.click('nav a:has-text("Dashboard")'),
      ])
      const authHeader = await request.headerValue('authorization')
      expect(authHeader).toMatch(/^Bearer\s+\S+/)
    })
  })
})
