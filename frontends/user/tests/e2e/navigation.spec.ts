import { test, expect } from '@playwright/test'
import { setupMocks, loginUser, loginViaForm } from './helpers/mock-api'

test.describe('Navigation & Route Guards', () => {
  test.beforeEach(async ({ page }) => {
    await setupMocks(page)
  })

  test('unauthenticated user is redirected to login', async ({ page }) => {
    await page.goto('/')
    await expect(page).toHaveURL(/\/login/)
  })

  test('unauthenticated user can access login page', async ({ page }) => {
    await page.goto('/login')
    await expect(page.getByRole('heading', { name: /sign in/i })).toBeVisible()
  })

  test('unauthenticated user can access register page', async ({ page }) => {
    await page.goto('/register')
    await expect(page.getByRole('heading', { name: /create/i })).toBeVisible()
  })

  test('authenticated user is redirected from login to dashboard', async ({ page }) => {
    await loginUser(page)
    await page.goto('/login')
    await expect(page).toHaveURL('/')
  })

  test('top navigation shows all links when authenticated', async ({ page }) => {
    await loginUser(page)
    await expect(page.locator('nav a:has-text("Overview")')).toBeVisible()
    await expect(page.locator('nav a:has-text("Profile")')).toBeVisible()
    await expect(page.locator('nav a:has-text("Security")')).toBeVisible()
    await expect(page.locator('nav a:has-text("Authorized Apps")')).toBeVisible()
  })

  test('navigation links work correctly', async ({ page }) => {
    await loginUser(page)

    await page.click('nav a:has-text("Profile")')
    await expect(page).toHaveURL('/profile')

    await page.click('nav a:has-text("Security")')
    await expect(page).toHaveURL('/security')

    await page.click('nav a:has-text("Authorized Apps")')
    await expect(page).toHaveURL('/authorized-apps')

    await page.click('nav a:has-text("Overview")')
    await expect(page).toHaveURL('/')
  })

  test('sign out clears session and redirects to login', async ({ page }) => {
    await loginUser(page)
    // Click the user avatar button to open dropdown
    await page.locator('header button:has(div.rounded-full)').click()
    await page.waitForSelector('button:has-text("Sign Out")', { state: 'visible' })
    await page.locator('button:has-text("Sign Out")').click()
    // After sign out, should eventually be on login page
    await expect(page).toHaveURL(/\/login/, { timeout: 15000 })
  })

  test('after sign out, protected routes redirect to login', async ({ page }) => {
    // Use a fresh page without addInitScript
    await page.goto('/login')
    // Set token via evaluate (not addInitScript)
    await page.evaluate(() => {
      localStorage.setItem('access_token', 'mock-access-token')
      localStorage.setItem('refresh_token', 'mock-refresh-token')
    })
    await page.goto('/')
    await page.waitForLoadState('networkidle')
    // Now clear tokens
    await page.evaluate(() => {
      localStorage.removeItem('access_token')
      localStorage.removeItem('refresh_token')
    })
    // Navigate to protected route on a fresh page load
    await page.goto('/profile')
    await expect(page).toHaveURL(/\/login/, { timeout: 10000 })
  })

  test('user dropdown opens and closes', async ({ page }) => {
    await loginUser(page)
    const avatarButton = page.locator('header button:has(div.rounded-full)')
    await avatarButton.click()
    // Dropdown should be visible
    await expect(page.locator('text=Sign Out')).toBeVisible()
    // Click avatar again to close
    await avatarButton.click()
    await page.waitForTimeout(300)
    // Dropdown items should be hidden
    await expect(page.locator('button:has-text("Sign Out")')).not.toBeVisible()
  })

  test('click outside dropdown closes it', async ({ page }) => {
    await loginUser(page)
    const avatarButton = page.locator('header button:has(div.rounded-full)')
    await avatarButton.click()
    await expect(page.locator('text=Sign Out')).toBeVisible()
    // Click the overlay/background
    await page.locator('.fixed.inset-0').click()
    await page.waitForTimeout(300)
    await expect(page.locator('button:has-text("Sign Out")')).not.toBeVisible()
  })

  test('active nav link is highlighted', async ({ page }) => {
    await loginUser(page)
    await page.click('nav a:has-text("Security")')
    await page.waitForURL('/security')
    const securityLink = page.locator('nav a:has-text("Security")')
    const classes = await securityLink.getAttribute('class')
    expect(classes).toContain('indigo')
  })
})
