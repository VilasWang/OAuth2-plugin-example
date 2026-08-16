import { test, expect } from '@playwright/test'
import { setupAuthenticatedMocks, loginAsAdmin, MOCK_USERS } from './helpers/mock-api'

test.describe('User Management', () => {
  test.beforeEach(async ({ page }) => {
    await setupAuthenticatedMocks(page)
    await loginAsAdmin(page)
    await page.click('nav a:has-text("Users")')
    await page.waitForURL('**/admin/users')
  })

  test('displays users list with correct columns', async ({ page }) => {
    await expect(page.locator('h2')).toContainText('Users')
    await expect(page.locator('th:has-text("Username")')).toBeVisible()
    await expect(page.locator('th:has-text("Email")')).toBeVisible()
    await expect(page.locator('th:has-text("Verified")')).toBeVisible()
    await expect(page.locator('th:has-text("MFA")')).toBeVisible()
    await expect(page.locator('th:has-text("Actions")')).toBeVisible()
  })

  test('shows all users with correct data', async ({ page }) => {
    // Check usernames in the table - use exact text matching
    const tableBody = page.locator('tbody')
    await expect(tableBody.getByRole('cell', { name: 'admin', exact: true })).toBeVisible()
    await expect(tableBody.getByRole('cell', { name: 'testuser', exact: true })).toBeVisible()
  })

  test('displays email verification status badges', async ({ page }) => {
    await expect(page.locator('span:has-text("Verified")')).toBeVisible()
    await expect(page.locator('span:has-text("Pending")')).toBeVisible()
  })

  test('displays MFA status badges', async ({ page }) => {
    await expect(page.locator('span:has-text("Enabled")')).toBeVisible()
    await expect(page.locator('span:has-text("Off")')).toBeVisible()
  })

  test('opens role assignment modal', async ({ page }) => {
    await page.click('button:has-text("Assign Roles")')
    await expect(page.locator('h3:has-text("Assign Roles")')).toBeVisible()
    await expect(page.locator('input[placeholder="admin, user"]')).toBeVisible()
  })

  test('role modal shows selected username', async ({ page }) => {
    // Click first "Assign Roles" button (admin user)
    await page.locator('button:has-text("Assign Roles")').first().click()
    await expect(page.locator('strong:has-text("admin")')).toBeVisible()
  })

  test('assigns roles successfully', async ({ page }) => {
    let roleRequestBody: any = null
    await page.route('**/api/admin/users/*/roles', async (route) => {
      roleRequestBody = JSON.parse(route.request().postData() || '{}')
      await route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify({ message: 'Roles updated' }),
      })
    })

    await page.locator('button:has-text("Assign Roles")').first().click()
    await page.fill('input[placeholder="admin, user"]', 'admin, editor')
    await page.click('button:has-text("Save Roles")')

    // Modal should close
    await expect(page.locator('h3:has-text("Assign Roles")')).not.toBeVisible()
    // Verify request body
    expect(roleRequestBody).toEqual({ roles: ['admin', 'editor'] })
  })

  test('cancel closes role modal', async ({ page }) => {
    await page.locator('button:has-text("Assign Roles")').first().click()
    await expect(page.locator('h3:has-text("Assign Roles")')).toBeVisible()
    await page.click('button:has-text("Cancel")')
    await expect(page.locator('h3:has-text("Assign Roles")')).not.toBeVisible()
  })

  test('assign multiple roles comma-separated', async ({ page }) => {
    let roleRequestBody: any = null
    await page.route('**/api/admin/users/*/roles', async (route) => {
      if (route.request().method() === 'PUT') {
        roleRequestBody = JSON.parse(route.request().postData() || '{}')
        await route.fulfill({ status: 200, contentType: 'application/json', body: JSON.stringify({ message: 'Roles updated' }) })
      } else {
        await route.continue()
      }
    })
    await page.locator('button:has-text("Assign Roles")').first().click()
    await page.fill('input[placeholder="admin, user"]', 'admin, user, editor')
    await page.click('button:has-text("Save Roles")')
    await page.waitForTimeout(300)
    expect(roleRequestBody).toEqual({ roles: ['admin', 'user', 'editor'] })
  })

  test('empty role input prevents save', async ({ page }) => {
    let apiCalled = false
    await page.route('**/api/admin/users/*/roles', async (route) => {
      if (route.request().method() === 'PUT') {
        apiCalled = true
      }
      await route.continue()
    })
    await page.locator('button:has-text("Assign Roles")').first().click()
    // Leave role input empty, click save
    await page.click('button:has-text("Save Roles")')
    await page.waitForTimeout(300)
    // API should not have been called (empty input guard)
    expect(apiCalled).toBe(false)
  })

  // A-USR-RL-005: assigning a non-existent role must surface the backend error
  // through the Frontend_Error_Module. normalizeError maps the error CODE to a
  // localized message (Requirement 8.6) — the raw backend message is not shown.
  // VALIDATION_RESOURCE_NOT_FOUND is the catalog code for a missing resource.
  test('non-existent role shows error message (A-USR-RL-005)', async ({ page }) => {
    await page.route('**/api/admin/users/*/roles', async (route) => {
      if (route.request().method() === 'PUT') {
        await route.fulfill({
          status: 400,
          contentType: 'application/json',
          body: JSON.stringify({
            error: {
              code: 'VALIDATION_RESOURCE_NOT_FOUND',
              category: 'VALIDATION',
              message: 'Role does not exist',
            },
          }),
        })
      } else {
        await route.continue()
      }
    })
    await page.locator('button:has-text("Assign Roles")').first().click()
    await page.fill('input[placeholder="admin, user"]', 'superadmin')
    await page.click('button:has-text("Save Roles")')
    // VALIDATION_RESOURCE_NOT_FOUND is mapped to "资源不存在"; the raw backend
    // message is intentionally NOT surfaced.
    const errorEl = page.locator('.bg-red-50')
    await expect(errorEl.first()).toBeVisible()
    await expect(errorEl.first()).toContainText('资源不存在')
    await expect(errorEl.first()).not.toContainText('Role does not exist')
  })

  test('API error displays error message', async ({ page }) => {
    // Trailing `*` so the override also matches the list call's query string
    // (?page=1&per_page=50) — without it the base mock answers 200 instead.
    await page.route('**/api/admin/users*', async (route) => {
      await route.fulfill({
        status: 500,
        contentType: 'application/json',
        body: JSON.stringify({ error: { code: 'INTERNAL_ERROR', message: 'Server error' } }),
      })
    })
    // Navigate away and back to trigger re-fetch
    await page.click('nav a:has-text("Dashboard")')
    await page.click('nav a:has-text("Users")')
    await page.waitForLoadState('networkidle')
    const errorEl = page.locator('.bg-red-50, .text-red-700')
    await expect(errorEl.first()).toBeVisible()
  })

  test('opens Create User modal', async ({ page }) => {
    await page.click('button:has-text("Create User")')
    await expect(page.locator('h3:has-text("Create User")')).toBeVisible()
    await expect(page.locator('input[placeholder="newuser"]')).toBeVisible()
  })

  test('creates a user successfully', async ({ page }) => {
    let createRequestBody: any = null
    await page.route('**/api/admin/users', async (route) => {
      if (route.request().method() === 'POST') {
        createRequestBody = JSON.parse(route.request().postData() || '{}')
        await route.fulfill({
          status: 201,
          contentType: 'application/json',
          body: JSON.stringify({ status: 'success', message: 'User created successfully', user: { id: 99, username: 'newuser_test', email: 'new@test.com' } }),
        })
      } else {
        await route.continue()
      }
    })
    await page.click('button:has-text("Create User")')
    await page.fill('input[placeholder="newuser"]', 'newuser_test')
    await page.fill('input[placeholder="••••••••"]', 'TestPass123!')
    // The modal-specific create button (the page-level button is behind the
    // modal overlay and would intercept pointer events).
    await page.locator('.fixed button:has-text("Create User")').click()
    await expect(page.locator('.bg-green-50')).toBeVisible({ timeout: 5000 })
    expect(createRequestBody.username).toBe('newuser_test')
  })

  test('search filters user list', async ({ page }) => {
    // Override the users route to return a filtered result for "testuser"
    await page.route('**/api/admin/users**', async (route) => {
      const url = new URL(route.request().url())
      const q = url.searchParams.get('q')
      if (q && q.length > 0) {
        await route.fulfill({
          status: 200,
          contentType: 'application/json',
          body: JSON.stringify({ status: 'success', page: 1, per_page: 50, total: 1, total_pages: 1, users: [MOCK_USERS[1]] }),
        })
      } else {
        await route.continue()
      }
    })
    await page.fill('input[placeholder="Search username or email..."]', 'testuser')
    await page.click('button:has-text("Search")')
    await page.waitForLoadState('networkidle')
    const tableBody = page.locator('tbody')
    await expect(tableBody.getByRole('cell', { name: 'testuser', exact: true })).toBeVisible()
    // admin user should NOT be present
    await expect(tableBody.getByRole('cell', { name: 'admin', exact: true })).toBeHidden()
  })

  test('duplicate username shows error', async ({ page }) => {
    await page.route('**/api/admin/users', async (route) => {
      if (route.request().method() === 'POST') {
        await route.fulfill({
          status: 400,
          contentType: 'application/json',
          body: JSON.stringify({ error: { code: 'VALIDATION_USERNAME_TAKEN', message: 'Username already exists' } }),
        })
      } else {
        await route.continue()
      }
    })
    await page.click('button:has-text("Create User")')
    await page.fill('input[placeholder="newuser"]', 'admin')
    await page.fill('input[placeholder="••••••••"]', 'TestPass123!')
    await page.locator('.fixed button:has-text("Create User")').click()
    const errorEl = page.locator('.bg-red-50')
    await expect(errorEl.first()).toBeVisible({ timeout: 5000 })
  })
})
