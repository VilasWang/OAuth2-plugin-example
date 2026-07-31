import { test, expect } from '@playwright/test';

test.describe('API Documentation (Swagger UI)', () => {
  const API_DOCS_URL = 'http://localhost:5555/docs/api/';
  const BACKEND_URL = 'http://localhost:5555/health';

  // Swagger UI is stateful, run tests sequentially to avoid interference
  test.describe.configure({ mode: 'serial' });

  // Skip all tests in this suite if backend is not running
  test.beforeAll(async () => {
    try {
      const response = await fetch(BACKEND_URL, { signal: AbortSignal.timeout(3000) });
      if (!response.ok) {
        test.skip(true, 'Backend server is not running (unhealthy). Start OAuth2Server first.');
      }
    } catch {
      test.skip(true, 'Backend server is not reachable at localhost:5555. Start OAuth2Server first.');
    }
  });

  // Skip all tests in this suite if backend is not running
  test.beforeAll(async () => {
    try {
      const res = await fetch('http://localhost:5555/health');
      if (!res.ok) test.skip(true, 'Backend not reachable (non-200 from /health)');
    } catch {
      test.skip(true, 'Backend not running on localhost:5555 — skipping Swagger UI tests');
    }
  });

  test('should load Swagger UI successfully', async ({ page }) => {
    await page.goto(API_DOCS_URL);
    
    // Check if the title is correct
    await expect(page).toHaveTitle(/OAuth2 Authorization Server API Documentation/);
    
    // Wait for the spec to load
    const infoTitle = page.locator('.info .title');
    await expect(infoTitle).toBeVisible({ timeout: 10000 });
    await expect(infoTitle).toContainText('OAuth2 Authorization Server API');
  });

  const testEndpoints = [
    { path: '/health', expectedStatus: /200/ },
    { path: '/.well-known/openid-configuration', expectedStatus: /200/ },
  ];

  for (const endpoint of testEndpoints) {
    test(`should execute ${endpoint.path} via Try it out`, async ({ page }) => {
      await page.goto(API_DOCS_URL);

      // Find the specific endpoint operation block
      const opBlock = page.locator('.opblock').filter({ 
        has: page.locator('.opblock-summary-path').filter({ hasText: new RegExp(`^${endpoint.path.replace(/\//g, '\\/')}$`) })
      }).first();
      
      await opBlock.scrollIntoViewIfNeeded();
      
      // Expand it
      if (!await opBlock.evaluate(el => el.classList.contains('is-open'))) {
        await opBlock.locator('.opblock-summary').click();
      }

      // Click "Try it out" to enter execute mode
      const tryItOutBtn = opBlock.locator('button.try-out__btn');
      await tryItOutBtn.waitFor({ state: 'visible', timeout: 10000 });
      await tryItOutBtn.click();

      // Wait for Execute button to appear and click it
      const executeBtn = opBlock.locator('button.execute');
      await executeBtn.waitFor({ state: 'visible', timeout: 10000 });
      await executeBtn.click();

      // Wait for the live response section to appear after Execute
      // Swagger UI renders live responses in different containers depending on version
      const liveResponse = opBlock.locator('.responses-inner table').filter({
        has: page.locator('td.response-col_status')
      }).last();
      await expect(liveResponse).toBeVisible({ timeout: 20000 });

      // Get the status code from the live response body rows (exclude header with col_header class)
      const responseStatus = liveResponse.locator('td.response-col_status:not(.col_header)').first();
      await expect(responseStatus).toContainText(endpoint.expectedStatus, { timeout: 10000 });
      
      // Verify response body is present
      const responseBody = opBlock.locator('.responses-inner .microlight, .responses-inner .highlight-code, .responses-inner pre').first();
      await expect(responseBody).toBeVisible();
    });
  }

  test('/api/me endpoint exists in API docs', async ({ page }) => {
    await page.goto(API_DOCS_URL);

    // Verify the endpoint is listed in Swagger UI
    const mePath = page.locator('.opblock-summary-path').filter({ hasText: /^\/api\/me$/ });
    await expect(mePath.first()).toBeVisible({ timeout: 10000 });
  });

  test('should list all major tags (System, OAuth2, Admin, User Profile, etc.)', async ({ page }) => {
    await page.goto(API_DOCS_URL);

    const tags = ['System', 'OAuth2', 'Admin', 'User Profile', 'MFA', 'WebAuthn'];
    for (const tag of tags) {
      const tagElement = page.locator('.opblock-tag').filter({ hasText: tag });
      await expect(tagElement.first()).toBeVisible();
    }
  });
  test('should find /oauth2/token endpoint and show its parameters', async ({ page }) => {
    await page.goto(API_DOCS_URL);

    // Find and expand the OAuth2 tag
    const oauth2Tag = page.locator('.opblock-tag').filter({ hasText: 'OAuth2' }).first();
    await oauth2Tag.scrollIntoViewIfNeeded();
    
    // Check if expanded (aria-expanded or checking if following sibling is visible)
    // A simple way is to click it if the endpoint isn't visible yet
    const tokenEndpoint = page.locator('.opblock-summary-path').filter({ hasText: '/oauth2/token' }).first();
    if (!await tokenEndpoint.isVisible()) {
      await oauth2Tag.click();
    }

    await tokenEndpoint.scrollIntoViewIfNeeded();
    await expect(tokenEndpoint).toBeVisible();
    await tokenEndpoint.click();

    // Check for some parameters
    const paramNames = ['grant_type', 'client_id', 'client_secret'];
    for (const name of paramNames) {
      const paramElement = page.locator('.parameter__name').filter({ hasText: name }).first();
      await expect(paramElement).toBeVisible({ timeout: 10000 });
    }
  });
});
