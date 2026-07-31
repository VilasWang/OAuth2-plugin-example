<script setup lang="ts">
import { ref } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import axios from 'axios'
import { normalizeError } from '../../services/errorAdapter'

const route = useRoute()
const router = useRouter()
const token = route.query.token as string || ''
const newPassword = ref('')
const confirmPassword = ref('')
const loading = ref(false)
const error = ref('')
const success = ref(false)

async function handleReset() {
  if (newPassword.value !== confirmPassword.value) { error.value = 'Passwords do not match'; return }
  if (newPassword.value.length < 6) { error.value = 'Password must be at least 6 characters'; return }
  error.value = ''
  loading.value = true
  try {
    await axios.post('/api/password-reset/confirm', { token, new_password: newPassword.value }, { headers: { 'Content-Type': 'application/json' } })
    success.value = true
    setTimeout(() => router.push('/login'), 3000)
  } catch (e: unknown) {
    error.value = normalizeError(e).message
  } finally {
    loading.value = false
  }
}
</script>

<template>
  <div class="min-h-screen flex items-center justify-center bg-gradient-to-br from-indigo-50 to-blue-100 px-4">
    <div class="w-full max-w-md">
      <div class="bg-white rounded-2xl shadow-xl p-8">
        <h1 class="text-2xl font-bold text-gray-900 text-center mb-6">Set New Password</h1>

        <div v-if="!token" class="text-center text-red-600">
          <p>Invalid or missing reset token.</p>
          <router-link to="/forgot-password" class="mt-4 inline-block text-indigo-600">Request a new link</router-link>
        </div>

        <div v-else-if="success" class="text-center space-y-3">
          <div class="w-16 h-16 bg-green-100 rounded-full flex items-center justify-center mx-auto"><span class="text-2xl">✅</span></div>
          <p class="text-gray-700 font-medium">Password reset successfully!</p>
          <p class="text-sm text-gray-500">Redirecting to login...</p>
        </div>

        <form v-else @submit.prevent="handleReset" class="space-y-4">
          <div v-if="error" class="p-3 bg-red-50 border border-red-200 text-red-700 rounded-lg text-sm">{{ error }}</div>
          <div>
            <label class="block text-sm font-medium text-gray-700 mb-1">New Password</label>
            <input v-model="newPassword" type="password" required autocomplete="new-password"
              class="block w-full px-4 py-3 border border-gray-300 rounded-lg focus:ring-2 focus:ring-indigo-500" />
          </div>
          <div>
            <label class="block text-sm font-medium text-gray-700 mb-1">Confirm Password</label>
            <input v-model="confirmPassword" type="password" required autocomplete="new-password"
              class="block w-full px-4 py-3 border border-gray-300 rounded-lg focus:ring-2 focus:ring-indigo-500" />
          </div>
          <button type="submit" :disabled="loading"
            class="w-full py-3 bg-indigo-600 text-white font-medium rounded-lg hover:bg-indigo-700 disabled:opacity-50">
            {{ loading ? 'Resetting...' : 'Reset Password' }}
          </button>
        </form>
      </div>
    </div>
  </div>
</template>
