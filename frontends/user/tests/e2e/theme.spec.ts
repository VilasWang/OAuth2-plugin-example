import { test, expect } from '@playwright/test'
import { setupMocks, loginUser } from './helpers/mock-api'

// Theme behavior (P4 review I4): store wiring, toggle entry, persistence,
// and the reduced-motion kill switch for the consent stamp.

test.describe('Theme', () => {
  test('default follows the OS color scheme', async ({ browser }) => {
    const ctx = await browser.newContext({ colorScheme: 'dark' })
    const page = await ctx.newPage()
    await page.goto('/login')
    await expect
      .poll(() => page.evaluate(() => document.documentElement.getAttribute('data-theme')))
      .toBe('dark')
    await ctx.close()
  })

  test('toggle flips data-theme, persists across reload, overrides OS', async ({ page }) => {
    await setupMocks(page)
    await loginUser(page)
    await page.goto('/')

    // Open the avatar menu and click the theme toggle.
    await page.locator('header button:has(div.rounded-full)').click()
    const toggle = page.getByRole('button', { name: 'Switch to dark theme' })
    await toggle.click()
    await expect
      .poll(() => page.evaluate(() => document.documentElement.getAttribute('data-theme')))
      .toBe('dark')
    expect(await page.evaluate(() => localStorage.getItem('fulla-theme'))).toBe('dark')

    // Reload: the persisted choice survives.
    await page.reload()
    await page.waitForTimeout(500)
    expect(await page.evaluate(() => document.documentElement.getAttribute('data-theme'))).toBe('dark')
  })

  test('consent stamp pulse has a reduced-motion kill switch', async ({ page }) => {
    await setupMocks(page)
    await loginUser(page)
    // ConsentPage is lazy — visiting the route loads its scoped styles.
    await page.goto('/consent?client_id=x&scope=openid&redirect_uri=http://e.com/cb&state=t&user_id=u1')
    await page.waitForTimeout(400)
    const has = await page.evaluate(() => {
      // The stamp pulse ships inside a prefers-reduced-motion media block
      // that sets animation: none on .stamp-pulse — assert the kill rule
      // exists in a loaded stylesheet.
      for (const sheet of Array.from(document.styleSheets)) {
        let rules: CSSRuleList
        try { rules = sheet.cssRules } catch { continue }
        for (const r of Array.from(rules)) {
          const media = r as CSSMediaRule
          if (media.media?.mediaText?.includes('prefers-reduced-motion')) {
            for (const inner of Array.from(media.cssRules)) {
              const t = (inner as CSSStyleRule).selectorText || ''
              if (t.includes('stamp-pulse') && (inner as CSSStyleRule).style.animationName === 'none') {
                return true
              }
            }
          }
        }
      }
      return false
    })
    expect(has).toBe(true)
  })
})
