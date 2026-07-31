<script setup lang="ts">
import { ref } from 'vue'
import { useRoute } from 'vue-router'
import axios from 'axios'
import { normalizeError } from '../../services/errorAdapter'

const route = useRoute()
const userCode = ref(route.query.user_code as string || '')
const loading = ref(false)
const status = ref<'input' | 'success' | 'error'>('input')
const error = ref('')

async function handleVerify() {
  if (!userCode.value.trim()) return
  loading.value = true
  error.value = ''
  try {
    await axios.post('/oauth2/device/verify', new URLSearchParams({ user_code: userCode.value.trim().toUpperCase() }))
    status.value = 'success'
  } catch (e: unknown) {
    error.value = normalizeError(e).message
    status.value = 'error'
  } finally {
    loading.value = false
  }
}
</script>

<template>
  <div class="min-h-screen flex items-center justify-center bg-gradient-to-br from-indigo-50 to-blue-100 px-4">
    <div class="w-full max-w-md bg-white rounded-2xl shadow-xl p-8">
      <div class="text-center mb-6">
        <h1 class="text-2xl font-bold text-gray-900">Device Authorization</h1>
        <p class="mt-2 text-gray-500">Enter the code shown on your device</p>
      </div>

      <div v-if="status === 'success'" class="text-center space-y-4">
        <div class="w-16 h-16 bg-green-100 rounded-full flex items-center justify-center mx-auto"><span class="text-3xl">✅</span></div>
        <p class="text-gray-700 font-medium">Device authorized successfully!</p>
        <p class="text-sm text-gray-500">You can close this page and return to your device.</p>
      </div>

      <div v-else-if="status === 'error'" class="text-center space-y-4">
        <div class="w-16 h-16 bg-red-100 rounded-full flex items-center justify-center mx-auto"><span class="text-3xl">❌</span></div>
        <p class="text-red-700">{{ error }}</p>
        <button @click="status = 'input'; error = ''" class="text-indigo-600 hover:text-indigo-800">Try again</button>
      </div>

      <form v-else @submit.prevent="handleVerify" class="space-y-5">
        <div>
          <label class="block text-sm font-medium text-gray-700 mb-1">Device Code</label>
          <input v-model="userCode" type="text" required
            class="block w-full px-4 py-3 text-center text-xl tracking-widest uppercase border border-gray-300 rounded-lg focus:ring-2 focus:ring-indigo-500"
            placeholder="ABCD-EFGH" />
        </div>
        <button type="submit" :disabled="loading || !userCode.trim()"
          class="w-full py-3 bg-indigo-600 text-white font-medium rounded-lg hover:bg-indigo-700 disabled:opacity-50">
          {{ loading ? 'Verifying...' : 'Authorize Device' }}
        </button>
      </form>
    </div>
  </div>
</template>
