import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'
import tailwindcss from '@tailwindcss/vite'

export default defineConfig({
  plugins: [vue(), tailwindcss()],
  server: {
    port: 5173,
    proxy: {
      '/api': {
        target: 'http://127.0.0.1:5555',
        changeOrigin: true,
      },
      '/oauth2': {
        target: 'http://127.0.0.1:5555',
        changeOrigin: true,
      },
      '/health': {
        target: 'http://127.0.0.1:5555',
        changeOrigin: true,
      },
      '/.well-known': {
        target: 'http://127.0.0.1:5555',
        changeOrigin: true,
      },
    },
  },
})
