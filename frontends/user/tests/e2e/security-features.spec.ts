import { test, expect } from '@playwright/test'
import { setupMocks, loginUser, MOCK_BACKUP_CODES } from './helpers/mock-api'

// Coverage for the gap-analysis zero-coverage features fixed in the P0/P1
// round: WebAuthn registration flow, one-time MFA backup codes, MFA disable.

test.describe('Security features (WebAuthn / MFA)', () => {
  test.beforeEach(async ({ page }) => {
    await setupMocks(page)
    await loginUser(page)
  })

  test('registers a passkey with the backend credential contract', async ({ page }) => {
    // Stub the WebAuthn browser API before any page script runs. The stub
    // mimics a real attestation response: rawId + getPublicKey() (SPKI DER).
    await page.addInitScript(() => {
      const fakeRawId = new Uint8Array([1, 2, 3, 4]).buffer
      const fakePublicKey = new Uint8Array([5, 6, 7, 8]).buffer
      Object.defineProperty(window, 'PublicKeyCredential', { value: window.PublicKeyCredential || class {}, configurable: true })
      Object.defineProperty(navigator, 'credentials', {
        value: {
          create: async () => ({
            id: 'stub-credential-id',
            rawId: fakeRawId,
            type: 'public-key',
            response: {
              attestationObject: new ArrayBuffer(8),
              clientDataJSON: new ArrayBuffer(8),
              getPublicKey: () => fakePublicKey,
            },
          }),
        },
        configurable: true,
      })
    })

    const finishRequest = page.waitForRequest('**/api/me/webauthn/register/finish')
    await page.goto('/security')
    await page.getByRole('button', { name: '+ Add Passkey' }).click()

    const request = await finishRequest
    const body = request.postDataJSON()
    // Backend contract: {credential_id, public_key, name} base64url strings —
    // NOT the browser attestation envelope the old code sent (gap-fix E1).
    expect(body.credential_id).toBeTruthy()
    expect(typeof body.credential_id).toBe('string')
    expect(body.public_key).toBeTruthy()
    expect(body.name).toBeTruthy()

    // List re-renders from the real credential shape (gap-fix E4).
    await expect(page.locator('text=My Passkey')).toBeVisible()
    await expect(page.locator('text=Sign counter: 0')).toBeVisible()
  })

  test('shows one-time backup codes after MFA setup and blocks until saved', async ({ page }) => {
    await page.goto('/security')

    await page.getByRole('button', { name: 'Enable MFA' }).click()
    await page.getByPlaceholder('000000').fill('123456')
    await page.getByRole('button', { name: 'Verify & Enable' }).click()

    // The one-shot recovery codes must be displayed (gap-fix P0: previously
    // the response was discarded and the codes were never reachable again).
    const dialog = page.getByRole('dialog', { name: /backup codes/i })
    await expect(dialog).toBeVisible()
    for (const code of [MOCK_BACKUP_CODES[0], MOCK_BACKUP_CODES[9]]) {
      await expect(dialog.locator('[data-testid="backup-code"]', { hasText: code })).toBeVisible()
    }

    // "I have safely saved my codes" is the only way out.
    await dialog.getByRole('button', { name: 'I have safely saved my codes' }).click()
    await expect(dialog).toBeHidden()
    await expect(page.locator('text=MFA enabled successfully!')).toBeVisible()
  })

  test('copies backup codes to the clipboard', async ({ browser }) => {
    // Clipboard access needs an explicit permission grant in Chromium —
    // first clipboard use in this repo (gap-fix review note).
    const context = await browser.newContext({ permissions: ['clipboard-read', 'clipboard-write'] })
    const page = await context.newPage()
    await setupMocks(page)
    await loginUser(page)

    await page.goto('/security')
    await page.getByRole('button', { name: 'Enable MFA' }).click()
    await page.getByPlaceholder('000000').fill('123456')
    await page.getByRole('button', { name: 'Verify & Enable' }).click()

    const dialog = page.getByRole('dialog', { name: /backup codes/i })
    await expect(dialog).toBeVisible()
    await dialog.getByRole('button', { name: 'Copy all' }).click()

    // Clipboard text uses platform line endings (\r\n on Windows) — split
    // tolerantly and compare the codes.
    const clipboard = await page.evaluate(() => navigator.clipboard.readText())
    expect(clipboard.split(/\r?\n/)).toEqual(MOCK_BACKUP_CODES)
    await context.close()
  })

  test('disables MFA from the security page', async ({ page }) => {
    // Profile with MFA already enabled so the disable branch renders.
    await page.route('**/api/me', async (route) => {
      if (route.request().method() === 'GET') {
        await route.fulfill({ status: 200, contentType: 'application/json', body: JSON.stringify({ username: 'testuser', email: 'test@example.com', email_verified: true, mfa_enabled: true }) })
      } else {
        await route.continue()
      }
    })

    await page.goto('/security')
    await page.getByPlaceholder('Your password').fill('password123')
    const disableRequest = page.waitForRequest('**/api/me/mfa/disable')
    await page.getByRole('button', { name: /Disable MFA/i }).click()

    const request = await disableRequest
    expect(request.postData()).toContain('password=password123')
    await expect(page.locator('text=MFA disabled')).toBeVisible()
  })
})
