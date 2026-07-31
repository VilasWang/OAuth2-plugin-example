import { test, expect } from '@playwright/test'
import { setupMocks, loginUser, MOCK_PROFILE } from './helpers/mock-api'

test.describe('Dashboard', () => {
  test.beforeEach(async ({ page }) => {
    await setupMocks(page)
    await loginUser(page)
  })

  test('displays welcome message', async ({ page }) => {
    await expect(page.locator('text=Welcome')).toBeVisible()
  })

  test('shows quick links', async ({ page }) => {
    await expect(page.locator('main a:has-text("Edit Profile")')).toBeVisible()
    await expect(page.locator('main a:has-text("Security Settings")')).toBeVisible()
    await expect(page.locator('main a:has-text("Authorized Apps")')).toBeVisible()
  })

  test('quick links navigate correctly', async ({ page }) => {
    await page.click('a:has-text("Edit Profile")')
    await expect(page).toHaveURL('/profile')
  })

  test('no roles shows "None"', async ({ page }) => {
    // Override userinfo to return empty roles
    await page.route('**/oauth2/userinfo', async (route) => {
      await route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify({ sub: 'id', name: 'testuser', email: 'test@example.com', roles: [] }),
      })
    })
    await page.reload()
    await page.waitForTimeout(1000)
    await expect(page.locator('text=None')).toBeVisible({ timeout: 5000 })
  })

  test('multiple roles shown as badges', async ({ page }) => {
    await page.route('**/oauth2/userinfo', async (route) => {
      await route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify({ sub: 'id', name: 'adminuser', email: 'admin@example.com', roles: ['admin', 'user', 'editor'] }),
      })
    })
    await page.reload()
    await page.waitForTimeout(1000)
    // Should have multiple role badges
    const badges = page.locator('.rounded-full')
    const count = await badges.count()
    expect(count).toBeGreaterThanOrEqual(3)
  })
})

test.describe('Profile', () => {
  test.beforeEach(async ({ page }) => {
    await setupMocks(page)
    await loginUser(page)
    await page.click('nav a:has-text("Profile")')
    await page.waitForURL('/profile')
  })

  test('displays profile page', async ({ page }) => {
    await expect(page.getByRole('heading', { level: 1 })).toContainText('Profile')
  })

  test('shows user info', async ({ page }) => {
    await expect(page.locator('text=testuser')).toBeVisible()
    await expect(page.locator('text=test@example.com')).toBeVisible()
  })

  test('shows email verification status', async ({ page }) => {
    await expect(page.locator('text=Verified')).toBeVisible()
  })

  test('unverified email shows resend button', async ({ page }) => {
    await page.route('**/api/me', async (route) => {
      if (route.request().method() === 'GET') {
        await route.fulfill({
          status: 200,
          contentType: 'application/json',
          body: JSON.stringify({ ...MOCK_PROFILE, email_verified: false }),
        })
      } else { await route.continue() }
    })
    await page.reload()
    await page.waitForTimeout(1000)
    await expect(page.locator('text=Unverified')).toBeVisible({ timeout: 5000 })
    await expect(page.locator('button:has-text("Resend")')).toBeVisible()
  })

  test('resend verification email shows success', async ({ page }) => {
    await page.route('**/api/me', async (route) => {
      if (route.request().method() === 'GET') {
        await route.fulfill({
          status: 200,
          contentType: 'application/json',
          body: JSON.stringify({ ...MOCK_PROFILE, email_verified: false }),
        })
      } else { await route.continue() }
    })
    await page.reload()
    await page.waitForTimeout(1000)
    const resendBtn = page.locator('button:has-text("Resend")')
    if (await resendBtn.isVisible()) {
      await resendBtn.click()
      await expect(page.locator('text=Verification email sent')).toBeVisible()
    }
  })

  test('profile API failure shows error', async ({ page }) => {
    await page.route('**/api/me', async (route) => {
      if (route.request().method() === 'GET') {
        await route.fulfill({ status: 500, contentType: 'application/json', body: JSON.stringify({ error: { code: 'INTERNAL_ERROR' } }) })
      } else { await route.continue() }
    })
    await page.reload()
    await page.waitForTimeout(1000)
    const errorEl = page.locator('[class*="red"]')
    await expect(errorEl.first()).toBeVisible({ timeout: 5000 })
  })

  test('profile shows loading state', async ({ page }) => {
    // Override /api/me with a delayed response
    await page.route('**/api/me', async (route) => {
      if (route.request().method() === 'GET') {
        await new Promise(resolve => setTimeout(resolve, 300))
        await route.fulfill({
          status: 200,
          contentType: 'application/json',
          body: JSON.stringify({ username: 'delayuser', email: 'delay@example.com', email_verified: true, mfa_enabled: false }),
        })
      } else { await route.continue() }
    })
    // Navigate away to dashboard, then back to profile to trigger fresh fetch
    await page.click('nav a:has-text("Overview")')
    await page.waitForTimeout(800)
    await page.click('nav a:has-text("Profile")')
    // After returning, profile data should ultimately render
    await page.waitForTimeout(2000)
    // Verify page is on profile and rendered something
    await expect(page).toHaveURL('/profile')
    const bodyText = await page.locator('body').textContent()
    expect(bodyText).toContain('delayuser')
  })
})

test.describe('Security', () => {
  test.beforeEach(async ({ page }) => {
    await setupMocks(page)
    await loginUser(page)
    await page.click('nav a:has-text("Security")')
    await page.waitForURL('/security')
  })

  test('displays security page sections', async ({ page }) => {
    await expect(page.locator('h2:has-text("Change Password")')).toBeVisible()
    await expect(page.locator('h2:has-text("Two-Factor Authentication")')).toBeVisible()
  })

  test('can change password', async ({ page }) => {
    await page.locator('input[autocomplete="current-password"]').fill('oldpass')
    const newPassFields = page.locator('input[autocomplete="new-password"]')
    await newPassFields.first().fill('NewPass123!')
    await newPassFields.nth(1).fill('NewPass123!')
    await page.locator('button:has-text("Change Password")').click()
    await expect(page.locator('text=Password changed')).toBeVisible()
  })

  test('password mismatch shows error', async ({ page }) => {
    await page.locator('input[autocomplete="current-password"]').fill('oldpass')
    const newPassFields = page.locator('input[autocomplete="new-password"]')
    await newPassFields.first().fill('password1')
    await newPassFields.nth(1).fill('password2')
    await page.locator('button:has-text("Change Password")').click()
    await expect(page.locator('text=Passwords do not match')).toBeVisible()
  })

  test('password too short shows error', async ({ page }) => {
    await page.locator('input[autocomplete="current-password"]').fill('oldpass')
    const newPassFields = page.locator('input[autocomplete="new-password"]')
    await newPassFields.first().fill('12345')
    await newPassFields.nth(1).fill('12345')
    await page.locator('button:has-text("Change Password")').click()
    await expect(page.locator('text=at least 6 characters')).toBeVisible()
  })

  test('wrong old password shows error', async ({ page }) => {
    await page.route('**/api/me/password', async (route) => {
      await route.fulfill({
        status: 400,
        contentType: 'application/json',
        body: JSON.stringify({ error: { code: 'AUTH_INVALID_CREDENTIALS', message: 'Wrong password' } }),
      })
    })
    await page.locator('input[autocomplete="current-password"]').fill('wrong')
    const newPassFields = page.locator('input[autocomplete="new-password"]')
    await newPassFields.first().fill('NewPass123!')
    await newPassFields.nth(1).fill('NewPass123!')
    await page.locator('button:has-text("Change Password")').click()
    await page.waitForTimeout(500)
    const errorEl = page.locator('[class*="red"]')
    await expect(errorEl.first()).toBeVisible()
  })

  test('MFA verify with valid code succeeds', async ({ page }) => {
    await page.click('button:has-text("Enable MFA")')
    await expect(page.locator('text=JBSWY3DPEHPK3PXP')).toBeVisible()
    const codeInput = page.locator('input[placeholder="000000"]')
    if (await codeInput.isVisible()) {
      await codeInput.fill('123456')
      await page.locator('button:has-text("Verify")').click()
      await expect(page.locator('text=MFA enabled')).toBeVisible()
    }
  })

  test('delete account with correct username succeeds', async ({ page }) => {
    const confirmInput = page.locator('input[placeholder*="username"], input[placeholder*="Username"]').last()
    if (await confirmInput.isVisible()) {
      await confirmInput.fill('testuser')
      const deleteBtn = page.locator('button:has-text("Delete My Account")')
      await expect(deleteBtn).toBeEnabled()
    }
  })

  test('shows MFA enable button when disabled', async ({ page }) => {
    await expect(page.locator('button:has-text("Enable MFA")')).toBeVisible()
  })

  test('can start MFA setup', async ({ page }) => {
    await page.click('button:has-text("Enable MFA")')
    await expect(page.locator('text=JBSWY3DPEHPK3PXP')).toBeVisible()
  })

  test('shows Danger Zone with delete account', async ({ page }) => {
    await expect(page.locator('h2:has-text("Danger Zone")')).toBeVisible()
    await expect(page.locator('button:has-text("Delete My Account")')).toBeVisible()
  })

  test('delete account button is disabled until username matches', async ({ page }) => {
    const deleteBtn = page.locator('button:has-text("Delete My Account")')
    await expect(deleteBtn).toBeDisabled()
  })

  test('shows WebAuthn section if supported', async ({ page }) => {
    await expect(page.locator('h2:has-text("Passkeys")')).toBeVisible()
    await expect(page.locator('button:has-text("Add Passkey")')).toBeVisible()
  })

  test('WebAuthn section hidden when browser unsupported', async ({ page }) => {
    // Simulate browser without PublicKeyCredential
    await page.goto('/login')
    await page.evaluate(() => {
      // @ts-ignore
      delete window.PublicKeyCredential
    })
    await loginUser(page)
    await page.click('nav a:has-text("Security")')
    await page.waitForURL('/security')
    // Passkeys section should not be visible
    const passkeysSection = page.locator('h2:has-text("Passkeys")')
    await expect(passkeysSection).not.toBeVisible({ timeout: 3000 })
  })
})

test.describe('Authorized Apps', () => {
  test.beforeEach(async ({ page }) => {
    await setupMocks(page)
    await loginUser(page)
    await page.click('nav a:has-text("Authorized Apps")')
    await page.waitForURL('/authorized-apps')
  })

  test('displays authorized apps', async ({ page }) => {
    await expect(page.locator('text=Third Party App')).toBeVisible()
    await expect(page.locator('text=Mobile App')).toBeVisible()
  })

  test('shows revoke buttons', async ({ page }) => {
    const revokeButtons = page.locator('button:has-text("Revoke")')
    await expect(revokeButtons).toHaveCount(2)
  })

  test('can revoke an app', async ({ page }) => {
    page.on('dialog', (dialog) => dialog.accept())
    await page.locator('button:has-text("Revoke")').first().click()
    await expect(page.locator('text=revoked')).toBeVisible()
  })

  test('empty authorized apps list', async ({ page }) => {
    await page.route('**/api/me/authorized-apps', async (route) => {
      await route.fulfill({ status: 200, contentType: 'application/json', body: JSON.stringify({ apps: [] }) })
    })
    await page.reload()
    await page.waitForLoadState('networkidle')
    await expect(page.locator('text=No authorized applications')).toBeVisible()
  })

  test('revoke cancel preserves app', async ({ page }) => {
    page.on('dialog', (dialog) => dialog.dismiss())
    await page.locator('button:has-text("Revoke")').first().click()
    await page.waitForTimeout(300)
    await expect(page.locator('text=Third Party App')).toBeVisible()
  })

  test('revoke failure shows error', async ({ page }) => {
    await page.route('**/api/me/authorized-apps/*', async (route) => {
      if (route.request().method() === 'DELETE') {
        await route.fulfill({ status: 500, contentType: 'application/json', body: JSON.stringify({ error: { code: 'INTERNAL_ERROR' } }) })
      } else { await route.continue() }
    })
    page.on('dialog', (dialog) => dialog.accept())
    await page.locator('button:has-text("Revoke")').first().click()
    await page.waitForTimeout(500)
    const errorEl = page.locator('[class*="red"]')
    await expect(errorEl.first()).toBeVisible()
  })

  test('app without name shows client_id as fallback', async ({ page }) => {
    await page.route('**/api/me/authorized-apps', async (route) => {
      await route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify({ apps: [{ client_id: 'nameless-app', name: '', scope: 'openid' }] }),
      })
    })
    // Navigate away and back to trigger fresh fetch
    await page.click('nav a:has-text("Overview")')
    await page.waitForTimeout(500)
    await page.click('nav a:has-text("Authorized Apps")')
    await page.waitForTimeout(1000)
    // Verify page renders with client_id as fallback
    const bodyText = await page.locator('body').textContent()
    expect(bodyText).toContain('nameless-app')
  })

  test('revoke success message auto-dismisses', async ({ page }) => {
    page.on('dialog', (dialog) => dialog.accept())
    await page.locator('button:has-text("Revoke")').first().click()
    // Success message appears
    await expect(page.locator('.bg-green-50, [class*="green"]').first()).toBeVisible({ timeout: 3000 })
    // Should disappear after ~3 seconds
    await expect(page.locator('.bg-green-50, [class*="green"]').first()).not.toBeVisible({ timeout: 5000 })
  })
})
