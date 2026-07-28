<script setup lang="ts">
import { ref } from 'vue'
import { useRoute } from 'vue-router'
import axios from 'axios'
import { useAuthStore } from '../../stores/auth'

const route = useRoute()
const auth = useAuthStore()
const loading = ref(false)

const clientId = route.query.client_id as string || ''
const scope = route.query.scope as string || ''
const redirectUri = route.query.redirect_uri as string || ''
const state = route.query.state as string || ''

const scopes = scope.split(' ').filter(Boolean)

const scopeDescriptions: Record<string, string> = {
  openid: 'Verify your identity',
  profile: 'Access your basic profile (username)',
  email: 'Access your email address',
  read: 'Read your data',
  write: 'Modify your data',
  admin: 'Administrative access',
}

async function handleConsent(action: 'approve' | 'deny') {
  loading.value = true
  try {
    const resp = await axios.post('/oauth2/consent', new URLSearchParams({
      client_id: clientId,
      user_id: auth.user?.sub || '',
      scope,
      redirect_uri: redirectUri,
      state,
      action,
    }))
    // Server returns redirect_uri with code
    if (resp.data?.redirect_uri) {
      window.location.href = resp.data.redirect_uri
    }
  } catch (e: any) {
    // If 302 redirect, browser handles it
    if (e.response?.status === 302) {
      const location = e.response.headers?.location
      if (location) window.location.href = location
    }
  } finally {
    loading.value = false
  }
}
</script>

<template>
  <div class="min-h-screen flex items-center justify-center bg-gradient-to-br from-indigo-50 to-blue-100 px-4">
    <div class="w-full max-w-md bg-white rounded-2xl shadow-xl p-8">
      <div class="text-center mb-6">
        <h1 class="text-2xl font-bold text-gray-900">Authorize Application</h1>
        <p class="mt-2 text-gray-500">
          <strong class="text-gray-700">{{ clientId }}</strong> is requesting access to your account
        </p>
      </div>

      <!-- Requested Permissions -->
      <div class="bg-gray-50 rounded-lg p-4 mb-6">
        <p class="text-sm font-medium text-gray-700 mb-3">This application will be able to:</p>
        <ul class="space-y-2">
          <li v-for="s in scopes" :key="s" class="flex items-center gap-2 text-sm text-gray-600">
            <span class="w-5 h-5 bg-indigo-100 text-indigo-600 rounded-full flex items-center justify-center text-xs">&#10003;</span>
            {{ scopeDescriptions[s] || s }}
          </li>
        </ul>
      </div>

      <!-- Signed in as -->
      <div class="text-center text-sm text-gray-500 mb-6">
        Signed in as <strong>{{ auth.user?.name || auth.user?.sub }}</strong>
      </div>

      <!-- Actions -->
      <div class="flex gap-3">
        <button @click="handleConsent('deny')" :disabled="loading"
          class="flex-1 py-3 border border-gray-300 text-gray-700 font-medium rounded-lg hover:bg-gray-50 disabled:opacity-50">
          Deny
        </button>
        <button @click="handleConsent('approve')" :disabled="loading"
          class="flex-1 py-3 bg-indigo-600 text-white font-medium rounded-lg hover:bg-indigo-700 disabled:opacity-50">
          {{ loading ? 'Authorizing...' : 'Authorize' }}
        </button>
      </div>
    </div>
  </div>
</template>
