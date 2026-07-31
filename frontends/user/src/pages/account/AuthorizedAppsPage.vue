<script setup lang="ts">
import { ref, onMounted } from 'vue'
import http from '../../services/http'
import { normalizeError } from '../../services/errorAdapter'

const apps = ref<any[]>([])
const loading = ref(true)
const error = ref('')
const success = ref('')

async function fetchApps() {
  loading.value = true
  try {
    const resp = await http.get('/api/me/authorized-apps')
    apps.value = resp.data.apps || resp.data.authorized_apps || []
  } catch (e: any) {
    error.value = 'Failed to load authorized apps'
  } finally {
    loading.value = false
  }
}

async function revokeApp(clientId: string, appName: string) {
  if (!confirm(`Revoke access for "${appName}"? This app will no longer be able to access your data.`)) return
  try {
    await http.delete(`/api/me/authorized-apps/${clientId}`)
    success.value = `Access revoked for "${appName}"`
    setTimeout(() => { success.value = '' }, 3000)
    await fetchApps()
  } catch (e: unknown) {
    error.value = normalizeError(e).message
  }
}

onMounted(fetchApps)
</script>

<template>
  <div>
    <h1 class="text-2xl font-bold text-gray-900 mb-6">Authorized Applications</h1>
    <p class="text-gray-500 mb-6">These applications have been granted access to your account.</p>

    <div v-if="success" class="mb-4 p-3 bg-green-50 border border-green-200 text-green-700 rounded-lg text-sm">{{ success }}</div>
    <div v-if="error" class="mb-4 p-3 bg-red-50 border border-red-200 text-red-700 rounded-lg text-sm">{{ error }}</div>

    <div v-if="loading" class="text-center py-12 text-gray-500">Loading...</div>

    <div v-else-if="apps.length === 0" class="bg-white rounded-xl border border-gray-200 p-12 text-center">
      <p class="text-gray-400 text-lg">No authorized applications</p>
      <p class="text-gray-400 text-sm mt-2">When you authorize third-party apps, they'll appear here.</p>
    </div>

    <div v-else class="space-y-3">
      <div v-for="app in apps" :key="app.client_id" class="bg-white rounded-xl border border-gray-200 p-5 flex items-center justify-between">
        <div>
          <p class="font-medium text-gray-900">{{ app.name || app.client_id }}</p>
          <p class="text-sm text-gray-500 mt-0.5">Client ID: <code class="font-mono text-xs">{{ app.client_id }}</code></p>
          <p v-if="app.scope" class="text-xs text-gray-400 mt-1">Scopes: {{ app.scope }}</p>
        </div>
        <button @click="revokeApp(app.client_id, app.name || app.client_id)"
          class="px-3 py-1.5 text-sm text-red-600 border border-red-200 rounded-lg hover:bg-red-50 transition-colors">
          Revoke
        </button>
      </div>
    </div>
  </div>
</template>

