<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { useRouter } from 'vue-router'
import { useAuthStore } from '../stores/auth'
import { normalizeError } from '@/services/errorAdapter'

const router = useRouter()
const auth = useAuthStore()
const error = ref('')

onMounted(async () => {
  const params = new URLSearchParams(window.location.search)
  const code = params.get('code')
  const state = params.get('state')
  const errorParam = params.get('error')

  if (errorParam) {
    error.value = params.get('error_description') || errorParam
    return
  }

  if (!code || !state) {
    error.value = 'Missing authorization code or state'
    return
  }

  try {
    await auth.handleCallback(code, state)
    router.replace('/')
  } catch (e: unknown) {
    // Display the localized message via the Frontend_Error_Module (Req 10.2/10.3).
    error.value = normalizeError(e).message
  }
})
</script>

<template>
  <div class="min-h-screen flex items-center justify-center bg-gray-50">
    <div class="text-center" v-if="!error">
      <div class="animate-spin rounded-full h-12 w-12 border-b-2 border-indigo-600 mx-auto"></div>
      <p class="mt-4 text-gray-600">Completing sign in...</p>
    </div>
    <div class="max-w-md bg-white rounded-lg shadow p-6" v-else>
      <h2 class="text-lg font-semibold text-red-600">Authentication Error</h2>
      <p class="mt-2 text-sm text-gray-600">{{ error }}</p>
      <router-link to="/login" class="mt-4 inline-block text-indigo-600 hover:text-indigo-500 text-sm">
        ← Back to login
      </router-link>
    </div>
  </div>
</template>
