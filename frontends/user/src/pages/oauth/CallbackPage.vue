<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { useRouter, useRoute } from 'vue-router'
import { useAuthStore } from '../../stores/auth'
import { normalizeError } from '../../services/errorAdapter'

const router = useRouter()
const route = useRoute()
const auth = useAuthStore()
const error = ref('')

onMounted(async () => {
  const code = route.query.code as string
  const errorParam = route.query.error as string

  if (errorParam) {
    error.value = route.query.error_description as string || errorParam
    return
  }

  if (!code) {
    error.value = 'No authorization code received'
    return
  }

  try {
    await auth.exchangeCode(code)
    router.replace('/')
  } catch (e: unknown) {
    error.value = normalizeError(e).message
  }
})
</script>

<template>
  <div class="min-h-screen flex items-center justify-center">
    <div class="text-center">
      <div v-if="error" class="p-6 bg-red-50 border border-red-200 rounded-lg max-w-md">
        <p class="text-red-700 font-medium">Authentication Error</p>
        <p class="text-red-600 text-sm mt-2">{{ error }}</p>
        <router-link to="/login" class="mt-4 inline-block text-indigo-600 hover:text-indigo-800">Back to Login</router-link>
      </div>
      <div v-else>
        <div class="animate-spin w-8 h-8 border-4 border-indigo-600 border-t-transparent rounded-full mx-auto"></div>
        <p class="mt-4 text-gray-600">Completing sign in...</p>
      </div>
    </div>
  </div>
</template>
