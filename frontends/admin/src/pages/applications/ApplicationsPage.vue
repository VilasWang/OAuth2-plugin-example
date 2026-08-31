<script setup lang="ts">
import { ref, onMounted } from 'vue'
import axios from 'axios'
import { normalizeError } from '../../services/errorAdapter'
import AppEmptyState from '../../components/ui/AppEmptyState.vue'

const clients = ref<any[]>([])
const loading = ref(true)
const showCreateModal = ref(false)
const showSecretModal = ref(false)
const newClientSecret = ref('')
const errorMessage = ref('')
const createForm = ref({
  name: '',
  client_type: 'CONFIDENTIAL',
  redirect_uris: '',
  grant_types: ['authorization_code'] as string[],
  backchannel_logout_uri: '',
})
const creating = ref(false)

const AVAILABLE_GRANT_TYPES = [
  { value: 'authorization_code', label: 'Authorization Code', description: '标准授权码流程（推荐）' },
  { value: 'refresh_token', label: 'Refresh Token', description: '允许刷新访问令牌' },
  { value: 'client_credentials', label: 'Client Credentials', description: '服务间通信（M2M）' },
  { value: 'urn:ietf:params:oauth:grant-type:device_code', label: 'Device Code', description: '无浏览器设备授权' },
]

// Inline error banner (replaces native alert for backend errors, Req 10.6).
function showError(msg: string) {
  errorMessage.value = msg
  setTimeout(() => { errorMessage.value = '' }, 5000)
}

async function fetchClients() {
  loading.value = true
  try {
    const resp = await axios.get('/api/admin/clients')
    clients.value = resp.data.clients || []
  } catch (e: unknown) {
    showError(normalizeError(e).message)
  } finally {
    loading.value = false
  }
}

async function createClient() {
  if (createForm.value.grant_types.length === 0) {
    showError('Please select at least one grant type')
    return
  }
  creating.value = true
  try {
    const body = {
      name: createForm.value.name,
      client_type: createForm.value.client_type,
      redirect_uris: createForm.value.redirect_uris,
      allowed_grant_types: createForm.value.grant_types.join(','),
      backchannel_logout_uri: createForm.value.backchannel_logout_uri,
    }
    const resp = await axios.post('/api/admin/clients', body, {
      headers: { 'Content-Type': 'application/json' },
    })
    newClientSecret.value = resp.data.client_secret || ''
    showCreateModal.value = false
    showSecretModal.value = true
    createForm.value = { name: '', client_type: 'CONFIDENTIAL', redirect_uris: '', grant_types: ['authorization_code'], backchannel_logout_uri: '' }
    await fetchClients()
  } catch (e: unknown) {
    // Req 10.3/10.6: normalize via Frontend_Error_Module, no native alert.
    showError(normalizeError(e).message)
  } finally {
    creating.value = false
  }
}

async function deleteClient(clientId: string) {
  if (!confirm(`Delete client "${clientId}"? This cannot be undone.`)) return
  try {
    await axios.delete(`/api/admin/clients/${clientId}`)
    await fetchClients()
  } catch (e: unknown) {
    showError(normalizeError(e).message)
  }
}

async function resetSecret(clientId: string) {
  if (!confirm(`Reset secret for "${clientId}"? The old secret will be immediately invalidated.`)) return
  try {
    const resp = await axios.post(`/api/admin/clients/${clientId}/reset-secret`)
    newClientSecret.value = resp.data.client_secret || ''
    showSecretModal.value = true
  } catch (e: unknown) {
    showError(normalizeError(e).message)
  }
}

onMounted(fetchClients)
</script>

<template>
  <div>
    <div class="flex justify-between items-center mb-6">
      <h2 class="text-2xl font-bold text-neutral-900">
        Applications
      </h2>
      <button
        class="px-4 py-2 bg-brand-600 text-white rounded-md hover:bg-brand-700 text-sm font-medium"
        @click="showCreateModal = true"
      >
        + Create Application
      </button>
    </div>

    <div
      v-if="errorMessage"
      class="mb-4 p-3 bg-error-50 border border-error-200 text-error-700 rounded-md text-sm"
    >
      {{ errorMessage }}
    </div>

    <div
      v-if="loading"
      class="text-center py-12 text-neutral-500"
    >
      Loading...
    </div>

    <AppEmptyState
      v-else-if="clients.length === 0"
      title="No applications registered yet"
      description="Register an OAuth2 client to start issuing tokens."
      action-label="Create your first application"
      @action="showCreateModal = true"
    />

    <div
      v-else
      class="bg-surface shadow rounded-lg overflow-hidden"
    >
      <table class="min-w-full divide-y divide-neutral-200">
        <thead class="bg-neutral-50">
          <tr>
            <th class="px-6 py-3 text-left text-xs font-medium text-neutral-500 uppercase">
              Name
            </th>
            <th class="px-6 py-3 text-left text-xs font-medium text-neutral-500 uppercase">
              Client ID
            </th>
            <th class="px-6 py-3 text-left text-xs font-medium text-neutral-500 uppercase">
              Type
            </th>
            <th class="px-6 py-3 text-left text-xs font-medium text-neutral-500 uppercase">
              Actions
            </th>
          </tr>
        </thead>
        <tbody class="bg-surface divide-y divide-neutral-200">
          <tr
            v-for="client in clients"
            :key="client.client_id"
            class="hover:bg-neutral-50"
          >
            <td class="px-6 py-3 text-sm font-medium text-neutral-900">
              <router-link
                :to="{ name: 'application-detail', params: { id: client.client_id } }"
                class="text-brand-600 hover:text-brand-800 hover:underline"
              >
                {{ client.name || client.client_id }}
              </router-link>
            </td>
            <td class="px-6 py-3 text-sm text-neutral-500 font-mono text-xs">
              {{ client.client_id }}
            </td>
            <td class="px-6 py-3">
              <span
                class="px-2 py-1 text-xs rounded-full"
                :class="client.client_type === 'PUBLIC' ? 'bg-brand-100 text-brand-800' : 'bg-info-100 text-info-700'"
              >
                {{ client.client_type }}
              </span>
            </td>
            <td class="px-6 py-3 text-sm space-x-2">
              <button
                v-if="client.client_type === 'CONFIDENTIAL'"
                class="px-2 py-1 rounded text-brand-600 hover:bg-brand-50 hover:text-brand-800 font-medium transition-colors"
                @click="resetSecret(client.client_id)"
              >
                Reset Secret
              </button>
              <button
                class="px-2 py-1 rounded text-error-600 hover:bg-error-50 hover:text-error-700 font-medium transition-colors"
                @click="deleteClient(client.client_id)"
              >
                Delete
              </button>
            </td>
          </tr>
        </tbody>
      </table>
    </div>

    <!-- Create Modal -->
    <div
      v-if="showCreateModal"
      class="fixed inset-0 bg-black/50 flex items-center justify-center z-50"
    >
      <div class="bg-surface rounded-lg shadow-xl p-6 w-full max-w-md">
        <h3 class="text-lg font-semibold mb-4">
          Create Application
        </h3>
        <form
          class="space-y-4"
          @submit.prevent="createClient"
        >
          <div>
            <label class="block text-sm font-medium text-neutral-700">Name</label>
            <input
              v-model="createForm.name"
              required
              class="mt-1 block w-full px-3 py-2 border border-neutral-300 rounded-md text-sm"
              placeholder="My App"
            >
          </div>
          <div>
            <label class="block text-sm font-medium text-neutral-700">Type</label>
            <select
              v-model="createForm.client_type"
              class="mt-1 block w-full px-3 py-2 border border-neutral-300 rounded-md text-sm"
            >
              <option value="CONFIDENTIAL">
                Confidential (Server-side)
              </option>
              <option value="PUBLIC">
                Public (SPA / Mobile)
              </option>
            </select>
          </div>
          <div>
            <label class="block text-sm font-medium text-neutral-700">Redirect URIs (comma-separated)</label>
            <input
              v-model="createForm.redirect_uris"
              class="mt-1 block w-full px-3 py-2 border border-neutral-300 rounded-md text-sm"
              placeholder="https://myapp.com/callback"
            >
          </div>
          <div>
            <label class="block text-sm font-medium text-neutral-700">Backchannel Logout URI (optional)</label>
            <input
              v-model="createForm.backchannel_logout_uri"
              class="mt-1 block w-full px-3 py-2 border border-neutral-300 rounded-md text-sm"
              placeholder="https://rp.example.com/backchannel-logout"
            >
            <p class="mt-1 text-xs text-neutral-500">
              OIDC Back-Channel Logout 1.0 (https). Leave empty to disable.
            </p>
          </div>
          <div>
            <label class="block text-sm font-medium text-neutral-700 mb-2">Grant Types</label>
            <div class="space-y-2">
              <label
                v-for="gt in AVAILABLE_GRANT_TYPES"
                :key="gt.value"
                class="flex items-start gap-2 cursor-pointer"
              >
                <input
                  v-model="createForm.grant_types"
                  type="checkbox"
                  :value="gt.value"
                  class="mt-0.5 h-4 w-4 rounded border-neutral-300 text-brand-600 focus:ring-brand-500"
                >
                <div>
                  <span class="text-sm font-medium text-neutral-700">{{ gt.label }}</span>
                  <p class="text-xs text-neutral-500">{{ gt.description }}</p>
                </div>
              </label>
            </div>
          </div>
          <div class="flex justify-end space-x-3 pt-2">
            <button
              type="button"
              class="px-4 py-2 border border-neutral-300 rounded-md text-sm"
              @click="showCreateModal = false"
            >
              Cancel
            </button>
            <button
              type="submit"
              :disabled="creating"
              class="px-4 py-2 bg-brand-600 text-white rounded-md text-sm hover:bg-brand-700 disabled:opacity-50"
            >
              {{ creating ? 'Creating...' : 'Create' }}
            </button>
          </div>
        </form>
      </div>
    </div>

    <!-- Secret Display Modal -->
    <div
      v-if="showSecretModal"
      class="fixed inset-0 bg-black/50 flex items-center justify-center z-50"
    >
      <div class="bg-surface rounded-lg shadow-xl p-6 w-full max-w-md">
        <h3 class="text-lg font-semibold mb-2">
          Client Secret
        </h3>
        <p class="text-sm text-error-600 mb-4">
          Copy this secret now. It will not be shown again.
        </p>
        <div class="bg-neutral-100 p-3 rounded-md font-mono text-sm break-all select-all">
          {{ newClientSecret }}
        </div>
        <div class="flex justify-end mt-4">
          <button
            class="px-4 py-2 bg-brand-600 text-white rounded-md text-sm"
            @click="showSecretModal = false; newClientSecret = ''"
          >
            Done
          </button>
        </div>
      </div>
    </div>
  </div>
</template>
