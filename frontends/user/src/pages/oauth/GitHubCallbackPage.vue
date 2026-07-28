<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { useRouter, useRoute } from 'vue-router'
import { useAuthStore } from '../../stores/auth'
import { setTokens } from '../../services/http'
import { normalizeError } from '../../services/errorAdapter'
import axios from 'axios'

const router = useRouter()
const route = useRoute()
const auth = useAuthStore()
const error = ref('')

onMounted(async () => {
  const code = route.query.code as string
  if (!code) {
    error.value = 'No authorization code from GitHub'
    return
  }

  try {
    const resp = await axios.post('/api/github/login', { code }, {
      headers: { 'Content-Type': 'application/json' },
    })

    if (resp.data.access_token) {
      // Backend returned tokens — complete login
      setTokens(resp.data.access_token, resp.data.refresh_token)
      auth.markAuthenticated()
      await auth.fetchUser()
      router.replace('/')
    } else {
      error.value = 'GitHub login did not return an access token'
    }
  } catch (e: unknown) {
    error.value = normalizeError(e).message
  }
})
</script>

<template>
  <div class="min-h-screen flex items-center justify-center">
    <div class="text-center max-w-md">
      <div v-if="error" class="p-6 bg-red-50 border border-red-200 rounded-lg">
        <p class="text-red-700 font-medium">GitHub Login Failed</p>
        <p class="text-red-600 text-sm mt-2">{{ error }}</p>
        <router-link to="/login" class="mt-4 inline-block text-indigo-600 hover:text-indigo-800">Back to Login</router-link>
      </div>
      <div v-else>
        <div class="animate-spin w-8 h-8 border-4 border-indigo-600 border-t-transparent rounded-full mx-auto"></div>
        <p class="mt-4 text-gray-600">Signing in with GitHub...</p>
      </div>
    </div>
  </div>
</template>
