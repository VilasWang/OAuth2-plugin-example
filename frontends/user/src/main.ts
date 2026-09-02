import { createApp } from 'vue'
import { createPinia } from 'pinia'
import App from './App.vue'
import router from './router'
import { i18n, initI18n } from './i18n'
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
app.use(createPinia())
app.use(router)
app.use(i18n)

// Apply the persisted/system theme before first paint of app content.
useThemeStore().init()

// Resolve the UI locale (chrome + error messages) before first mount —
// mirrors the theme store init pattern (ADR-0013).
initI18n()

app.mount('#app')
