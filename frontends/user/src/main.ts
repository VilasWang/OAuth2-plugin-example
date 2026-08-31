import { createApp } from 'vue'
import { createPinia } from 'pinia'
import App from './App.vue'
import router from './router'
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

// Apply the persisted/system theme before first paint of app content.
useThemeStore().init()

app.mount('#app')
