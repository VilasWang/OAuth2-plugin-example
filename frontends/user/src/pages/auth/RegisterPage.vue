<script setup lang="ts">
import { ref } from 'vue'
import { useRouter } from 'vue-router'
import axios from 'axios'
import { normalizeError } from '../../services/errorAdapter'

const router = useRouter()
const username = ref('')
const email = ref('')
const password = ref('')
const confirmPassword = ref('')
const error = ref('')
const loading = ref(false)
const success = ref(false)

async function handleRegister() {
  error.value = ''
  if (password.value !== confirmPassword.value) {
    error.value = 'Passwords do not match'
    return
  }
  if (password.value.length < 6) {
    error.value = 'Password must be at least 6 characters'
    return
  }
  loading.value = true
  try {
    await axios.post('/api/register', new URLSearchParams({
      username: username.value,
      password: password.value,
      email: email.value,
    }))
    success.value = true
    setTimeout(() => router.push('/login'), 2000)
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
        <div class="text-center mb-8">
          <h1 class="text-3xl font-bold text-gray-900">Create Account</h1>
          <p class="mt-2 text-gray-500">Join us today</p>
        </div>

        <div v-if="success" class="p-4 bg-green-50 border border-green-200 text-green-700 rounded-lg text-center">
          <p class="font-medium">Account created successfully!</p>
          <p class="text-sm mt-1">Redirecting to login...</p>
        </div>

        <div v-if="error" class="mb-4 p-3 bg-red-50 border border-red-200 text-red-700 rounded-lg text-sm">{{ error }}</div>

        <form v-if="!success" @submit.prevent="handleRegister" class="space-y-4">
          <div>
            <label class="block text-sm font-medium text-gray-700 mb-1">Email</label>
            <input v-model="email" type="email" required autocomplete="email"
              class="block w-full px-4 py-3 border border-gray-300 rounded-lg focus:ring-2 focus:ring-indigo-500 focus:border-indigo-500" />
          </div>
          <div>
            <label class="block text-sm font-medium text-gray-700 mb-1">Username <span class="text-gray-400 font-normal">(optional)</span></label>
            <input v-model="username" type="text" autocomplete="username"
              class="block w-full px-4 py-3 border border-gray-300 rounded-lg focus:ring-2 focus:ring-indigo-500 focus:border-indigo-500" />
          </div>
          <div>
            <label class="block text-sm font-medium text-gray-700 mb-1">Password</label>
            <input v-model="password" type="password" required autocomplete="new-password"
              class="block w-full px-4 py-3 border border-gray-300 rounded-lg focus:ring-2 focus:ring-indigo-500 focus:border-indigo-500" />
          </div>
          <div>
            <label class="block text-sm font-medium text-gray-700 mb-1">Confirm Password</label>
            <input v-model="confirmPassword" type="password" required autocomplete="new-password"
              class="block w-full px-4 py-3 border border-gray-300 rounded-lg focus:ring-2 focus:ring-indigo-500 focus:border-indigo-500" />
          </div>
          <button type="submit" :disabled="loading"
            class="w-full py-3 px-4 bg-indigo-600 text-white font-medium rounded-lg hover:bg-indigo-700 disabled:opacity-50 transition-colors">
            {{ loading ? 'Creating...' : 'Create Account' }}
          </button>
        </form>

        <p class="mt-6 text-center text-sm text-gray-500">
          Already have an account?
          <router-link to="/login" class="text-indigo-600 font-medium hover:text-indigo-800">Sign in</router-link>
        </p>
      </div>
    </div>
  </div>
</template>
