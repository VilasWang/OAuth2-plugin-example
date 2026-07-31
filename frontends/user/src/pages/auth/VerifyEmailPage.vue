<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { useRoute } from 'vue-router'
import axios from 'axios'
import { normalizeError } from '../../services/errorAdapter'

const route = useRoute()
const status = ref<'loading' | 'success' | 'error'>('loading')
const message = ref('')

onMounted(async () => {
  const token = route.query.token as string
  if (!token) {
    status.value = 'error'
    message.value = 'Missing verification token'
    return
  }
  try {
    const resp = await axios.get(`/api/verify-email?token=${encodeURIComponent(token)}`)
    status.value = 'success'
    message.value = resp.data?.message || 'Email verified successfully!'
  } catch (e: unknown) {
    status.value = 'error'
    message.value = normalizeError(e).message
  }
})
</script>

<template>
  <div class="min-h-screen flex items-center justify-center bg-gradient-to-br from-indigo-50 to-blue-100 px-4">
    <div class="w-full max-w-md bg-white rounded-2xl shadow-xl p-8 text-center">
      <div v-if="status === 'loading'">
        <div class="animate-spin w-8 h-8 border-4 border-indigo-600 border-t-transparent rounded-full mx-auto"></div>
        <p class="mt-4 text-gray-600">Verifying your email...</p>
      </div>
      <div v-else-if="status === 'success'">
        <div class="w-16 h-16 bg-green-100 rounded-full flex items-center justify-center mx-auto"><span class="text-3xl">✅</span></div>
        <h2 class="mt-4 text-xl font-bold text-gray-900">Email Verified!</h2>
        <p class="mt-2 text-gray-600">{{ message }}</p>
        <router-link to="/login" class="mt-6 inline-block px-6 py-2 bg-indigo-600 text-white rounded-lg hover:bg-indigo-700">Go to Login</router-link>
      </div>
      <div v-else>
        <div class="w-16 h-16 bg-red-100 rounded-full flex items-center justify-center mx-auto"><span class="text-3xl">❌</span></div>
        <h2 class="mt-4 text-xl font-bold text-gray-900">Verification Failed</h2>
        <p class="mt-2 text-gray-600">{{ message }}</p>
        <router-link to="/login" class="mt-6 inline-block px-6 py-2 bg-indigo-600 text-white rounded-lg hover:bg-indigo-700">Go to Login</router-link>
      </div>
    </div>
  </div>
</template>
