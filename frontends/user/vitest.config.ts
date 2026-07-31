import { fileURLToPath } from 'node:url'
import { defineConfig } from 'vitest/config'
import vue from '@vitejs/plugin-vue'

// Vitest configuration for unit / property-based tests.
// Unit tests cover pure functions (e.g. the shared error adapter and message
// catalog), so the lightweight `node` environment is sufficient and avoids a
// jsdom dependency. The Playwright e2e suite under `tests/e2e` is excluded so
// the two runners never collide.
export default defineConfig({
  plugins: [vue()],
  resolve: {
    alias: {
      '@': fileURLToPath(new URL('./src', import.meta.url)),
    },
  },
  test: {
    globals: true,
    environment: 'node',
    include: ['src/**/*.{test,spec}.ts', 'tests/unit/**/*.{test,spec}.ts'],
    exclude: ['node_modules/**', 'dist/**', 'tests/e2e/**'],
  },
})
