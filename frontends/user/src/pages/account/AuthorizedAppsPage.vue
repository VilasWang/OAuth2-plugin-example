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
    // Backend envelope: {authorized_apps: [...], total} (UserSelfServiceController).
    // The old `resp.data.apps ||` first fallback matched only a mock shape the
    // real backend never returns — PR-review cleanup removed it so the page
    // parses the actual contract.
    apps.value = resp.data?.authorized_apps || []
  } catch {
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
    <h1 class="text-2xl font-bold text-neutral-900 mb-6">
      Authorized Applications
    </h1>
    <p class="text-neutral-500 mb-6">
      These applications have been granted access to your account.
    </p>

    <div
      v-if="success"
      class="mb-4 p-3 bg-green-50 border border-green-200 text-green-700 rounded-lg text-sm"
    >
      {{ success }}
    </div>
    <div
      v-if="error"
      class="mb-4 p-3 bg-error-50 border border-error-200 text-error-700 rounded-lg text-sm"
    >
      {{ error }}
    </div>

    <div
      v-if="loading"
      class="text-center py-12 text-neutral-500"
    >
      Loading...
    </div>

    <div
      v-else-if="apps.length === 0"
      class="bg-surface rounded-xl border border-neutral-200 p-12 text-center"
    >
      <p class="text-neutral-400 text-lg">
        No authorized applications
      </p>
      <p class="text-neutral-400 text-sm mt-2">
        When you authorize third-party apps, they'll appear here.
      </p>
    </div>

    <div
      v-else
      class="space-y-3"
    >
      <div
        v-for="app in apps"
        :key="app.client_id"
        class="bg-surface rounded-xl border border-neutral-200 p-5 flex items-center justify-between"
      >
        <div>
          <p class="font-medium text-neutral-900">
            {{ app.name || app.client_id }}
          </p>
          <p class="text-sm text-neutral-500 mt-0.5">
            Client ID: <code class="font-mono text-xs">{{ app.client_id }}</code>
          </p>
          <p
            v-if="app.scope"
            class="text-xs text-neutral-400 mt-1"
          >
            Scopes: {{ app.scope }}
          </p>
        </div>
        <button
          class="px-3 py-1.5 text-sm text-error-600 border border-error-200 rounded-lg hover:bg-error-50 transition-colors"
          @click="revokeApp(app.client_id, app.name || app.client_id)"
        >
          Revoke
        </button>
      </div>
    </div>
  </div>
</template>

