<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { useRouter } from 'vue-router'
import axios from 'axios'
import { normalizeError } from '../../services/errorAdapter'

const router = useRouter()
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
    }
    const resp = await axios.post('/api/admin/clients', body, {
      headers: { 'Content-Type': 'application/json' },
    })
    newClientSecret.value = resp.data.client_secret || ''
    showCreateModal.value = false
    showSecretModal.value = true
    createForm.value = { name: '', client_type: 'CONFIDENTIAL', redirect_uris: '', grant_types: ['authorization_code'] }
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
      <h2 class="text-2xl font-bold text-gray-900">Applications</h2>
      <button @click="showCreateModal = true" class="px-4 py-2 bg-indigo-600 text-white rounded-md hover:bg-indigo-700 text-sm font-medium">
        + Create Application
      </button>
    </div>

    <div v-if="errorMessage" class="mb-4 p-3 bg-red-50 border border-red-200 text-red-700 rounded-md text-sm">{{ errorMessage }}</div>

    <div v-if="loading" class="text-center py-12 text-gray-500">Loading...</div>

    <div v-else-if="clients.length === 0" class="text-center py-12">
      <p class="text-gray-500 mb-4">No applications registered yet</p>
      <button @click="showCreateModal = true" class="px-4 py-2 bg-indigo-600 text-white rounded-md hover:bg-indigo-700 text-sm">Create your first application</button>
    </div>

    <div v-else class="bg-white shadow rounded-lg overflow-hidden">
      <table class="min-w-full divide-y divide-gray-200">
        <thead class="bg-gray-50">
          <tr>
            <th class="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase">Name</th>
            <th class="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase">Client ID</th>
            <th class="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase">Type</th>
            <th class="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase">Actions</th>
          </tr>
        </thead>
        <tbody class="bg-white divide-y divide-gray-200">
          <tr v-for="client in clients" :key="client.client_id" class="hover:bg-gray-50">
            <td class="px-6 py-4 text-sm font-medium text-gray-900">
              <router-link :to="{ name: 'application-detail', params: { id: client.client_id } }" class="text-indigo-600 hover:text-indigo-800 hover:underline">
                {{ client.name || client.client_id }}
              </router-link>
            </td>
            <td class="px-6 py-4 text-sm text-gray-500 font-mono text-xs">{{ client.client_id }}</td>
            <td class="px-6 py-4">
              <span class="px-2 py-1 text-xs rounded-full" :class="client.client_type === 'PUBLIC' ? 'bg-blue-100 text-blue-800' : 'bg-purple-100 text-purple-800'">
                {{ client.client_type }}
              </span>
            </td>
            <td class="px-6 py-4 text-sm space-x-2">
              <button v-if="client.client_type === 'CONFIDENTIAL'" @click="resetSecret(client.client_id)" class="px-2 py-1 rounded text-indigo-600 hover:bg-indigo-50 hover:text-indigo-800 font-medium transition-colors">Reset Secret</button>
              <button @click="deleteClient(client.client_id)" class="px-2 py-1 rounded text-red-600 hover:bg-red-50 hover:text-red-800 font-medium transition-colors">Delete</button>
            </td>
          </tr>
        </tbody>
      </table>
    </div>

    <!-- Create Modal -->
    <div v-if="showCreateModal" class="fixed inset-0 bg-black/50 flex items-center justify-center z-50">
      <div class="bg-white rounded-lg shadow-xl p-6 w-full max-w-md">
        <h3 class="text-lg font-semibold mb-4">Create Application</h3>
        <form @submit.prevent="createClient" class="space-y-4">
          <div>
            <label class="block text-sm font-medium text-gray-700">Name</label>
            <input v-model="createForm.name" required class="mt-1 block w-full px-3 py-2 border border-gray-300 rounded-md text-sm" placeholder="My App" />
          </div>
          <div>
            <label class="block text-sm font-medium text-gray-700">Type</label>
            <select v-model="createForm.client_type" class="mt-1 block w-full px-3 py-2 border border-gray-300 rounded-md text-sm">
              <option value="CONFIDENTIAL">Confidential (Server-side)</option>
              <option value="PUBLIC">Public (SPA / Mobile)</option>
            </select>
          </div>
          <div>
            <label class="block text-sm font-medium text-gray-700">Redirect URIs (comma-separated)</label>
            <input v-model="createForm.redirect_uris" class="mt-1 block w-full px-3 py-2 border border-gray-300 rounded-md text-sm" placeholder="https://myapp.com/callback" />
          </div>
          <div>
            <label class="block text-sm font-medium text-gray-700 mb-2">Grant Types</label>
            <div class="space-y-2">
              <label v-for="gt in AVAILABLE_GRANT_TYPES" :key="gt.value" class="flex items-start gap-2 cursor-pointer">
                <input
                  type="checkbox"
                  :value="gt.value"
                  v-model="createForm.grant_types"
                  class="mt-0.5 h-4 w-4 rounded border-gray-300 text-indigo-600 focus:ring-indigo-500"
                />
                <div>
                  <span class="text-sm font-medium text-gray-700">{{ gt.label }}</span>
                  <p class="text-xs text-gray-500">{{ gt.description }}</p>
                </div>
              </label>
            </div>
          </div>
          <div class="flex justify-end space-x-3 pt-2">
            <button type="button" @click="showCreateModal = false" class="px-4 py-2 border border-gray-300 rounded-md text-sm">Cancel</button>
            <button type="submit" :disabled="creating" class="px-4 py-2 bg-indigo-600 text-white rounded-md text-sm hover:bg-indigo-700 disabled:opacity-50">
              {{ creating ? 'Creating...' : 'Create' }}
            </button>
          </div>
        </form>
      </div>
    </div>

    <!-- Secret Display Modal -->
    <div v-if="showSecretModal" class="fixed inset-0 bg-black/50 flex items-center justify-center z-50">
      <div class="bg-white rounded-lg shadow-xl p-6 w-full max-w-md">
        <h3 class="text-lg font-semibold mb-2">Client Secret</h3>
        <p class="text-sm text-red-600 mb-4">Copy this secret now. It will not be shown again.</p>
        <div class="bg-gray-100 p-3 rounded-md font-mono text-sm break-all select-all">{{ newClientSecret }}</div>
        <div class="flex justify-end mt-4">
          <button @click="showSecretModal = false; newClientSecret = ''" class="px-4 py-2 bg-indigo-600 text-white rounded-md text-sm">Done</button>
        </div>
      </div>
    </div>
  </div>
</template>
