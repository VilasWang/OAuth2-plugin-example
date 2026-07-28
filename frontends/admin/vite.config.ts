import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'
import tailwindcss from '@tailwindcss/vite'
import path from 'path'
import { fileURLToPath } from 'url'

const __dirname = path.dirname(fileURLToPath(import.meta.url))

export default defineConfig({
  plugins: [vue(), tailwindcss()],
  base: '/admin/',
  resolve: {
    alias: {
      '@': path.resolve(__dirname, './src')
    },
    extensions: ['.ts', '.js', '.vue', '.json']
  },
  server: {
    port: 5174,
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
