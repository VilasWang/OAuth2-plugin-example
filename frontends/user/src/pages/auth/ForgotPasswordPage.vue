<script setup lang="ts">
import { ref } from 'vue'
import axios from 'axios'

const email = ref('')
const loading = ref(false)
const sent = ref(false)
const error = ref('')

async function handleSubmit() {
  error.value = ''
  loading.value = true
  try {
    await axios.post('/api/password-reset/request', { email: email.value }, { headers: { 'Content-Type': 'application/json' } })
    sent.value = true
  } catch (e: any) {
    // Always show success (anti-enumeration)
    sent.value = true
  } finally {
    loading.value = false
  }
}
</script>

<template>
  <div class="min-h-screen flex items-center justify-center bg-gradient-to-br from-indigo-50 to-blue-100 px-4">
    <div class="w-full max-w-md">
      <div class="bg-white rounded-2xl shadow-xl p-8">
        <div class="text-center mb-8">
          <h1 class="text-2xl font-bold text-gray-900">Reset Password</h1>
          <p class="mt-2 text-gray-500">Enter your email to receive a reset link</p>
        </div>

        <div v-if="sent" class="text-center space-y-4">
          <div class="w-16 h-16 bg-green-100 rounded-full flex items-center justify-center mx-auto">
            <span class="text-2xl">✉️</span>
          </div>
          <p class="text-gray-700">If an account with that email exists, we've sent a password reset link.</p>
          <p class="text-sm text-gray-500">Check your inbox and spam folder.</p>
          <router-link to="/login" class="inline-block mt-4 text-indigo-600 hover:text-indigo-800 font-medium">Back to Login</router-link>
        </div>

        <form v-else @submit.prevent="handleSubmit" class="space-y-5">
          <div>
            <label class="block text-sm font-medium text-gray-700 mb-1">Email Address</label>
            <input v-model="email" type="email" required autocomplete="email"
              class="block w-full px-4 py-3 border border-gray-300 rounded-lg focus:ring-2 focus:ring-indigo-500 focus:border-indigo-500"
              placeholder="you@example.com" />
          </div>
          <button type="submit" :disabled="loading"
            class="w-full py-3 px-4 bg-indigo-600 text-white font-medium rounded-lg hover:bg-indigo-700 disabled:opacity-50 transition-colors">
            {{ loading ? 'Sending...' : 'Send Reset Link' }}
          </button>
          <p class="text-center text-sm text-gray-500">
            <router-link to="/login" class="text-indigo-600 hover:text-indigo-800">Back to Login</router-link>
          </p>
        </form>
      </div>
    </div>
  </div>
</template>
