import { createApp } from 'vue'
import { createPinia } from 'pinia'
import router from './router'
import App from './App.vue'
import { i18n, initI18n } from './i18n'
import { useAuthStore } from './stores/auth'
import { useThemeStore } from './stores/theme'
import '@fontsource/dm-sans/400.css'
import '@fontsource/dm-sans/500.css'
import '@fontsource/dm-sans/600.css'
import '@fontsource/dm-sans/700.css'
import '@fontsource/noto-sans-sc/400.css'
import '@fontsource/noto-sans-sc/700.css'
import '@fontsource-variable/archivo'
import '@fontsource-variable/jetbrains-mono'
import './style.css'

const app = createApp(App)
const pinia = createPinia()
app.use(pinia)
app.use(router)
app.use(i18n)

// Attempt to restore an existing session (refresh token persisted in
// sessionStorage) before mounting + initial navigation. The auth guard reads
// isAuthenticated, which requires the access token to be present. See A-LOGIN-014.
// Fire-and-forget: the router's beforeEach guard awaits the deduped
// ensureSessionRestored() before allowing protected routes, so the one-shot
// session restoration completes before first render regardless. Top-level
// await would break the Vite es2020 build target (see frontends/user pattern).
useAuthStore().restoreSession()

// Apply the persisted/system theme before first paint of app content.
useThemeStore().init()

// Resolve the UI locale (chrome + error messages) before first mount —
// mirrors the theme store init pattern (ADR-0013).
initI18n()

app.mount('#app')
