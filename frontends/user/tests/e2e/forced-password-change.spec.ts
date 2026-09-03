import { test, expect } from '@playwright/test'

// PR #157 review MINOR 9: user-portal coverage for the forced first-login
// password change — the password_change_required login branch, the JSON
// contract of POST /oauth2/password/change, and the ?must_change_password=1
// landing (server 302 from /oauth2/authorize).

test.describe('Forced password change (user portal)', () => {
  test('login password_change_required shows the inline form; change posts JSON and succeeds', async ({ page }) => {
    await page.route('**/oauth2/login', async (route) => {
      await route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify({ password_change_required: true, message: 'Password change required.' }),
      })
    })
    const changeRequest = page.waitForRequest('**/oauth2/password/change')
    await page.route('**/oauth2/password/change', async (route) => {
      await route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify({ message: 'Password changed successfully.' }),
      })
    })

    await page.goto('/login')
    await page.fill('input[autocomplete="username"]', 'user@example.com')
    await page.fill('input[autocomplete="current-password"]', 'OldPassw0rd!')
    await page.click('button:has-text("Sign In")')

    // Inline change form replaces the login form.
    await expect(page.getByPlaceholder('Enter your current password')).toBeVisible()

    await page.getByPlaceholder('Enter your current password').fill('OldPassw0rd!')
    await page.getByPlaceholder('Enter your new password', { exact: true }).fill('NewPassw0rd!')
    await page.getByPlaceholder('Re-enter your new password').fill('NewPassw0rd!')
    await page.click('button:has-text("Change Password")')

    const request = await changeRequest
    expect((request.headers()['content-type'] || '').toLowerCase()).toContain('application/json')
    const body = JSON.parse(request.postData() || '{}')
    expect(body.old_password).toBe('OldPassw0rd!')
    expect(body.new_password).toBe('NewPassw0rd!')

    await expect(page.getByTestId('forced-password-change-done')).toBeVisible()
  })

  test('?must_change_password=1 shows the change form directly', async ({ page }) => {
    await page.goto('/login?must_change_password=1')
    await expect(page.getByPlaceholder('Enter your current password')).toBeVisible()
  })
})
