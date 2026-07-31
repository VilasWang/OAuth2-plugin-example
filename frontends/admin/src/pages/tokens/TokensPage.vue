<script setup lang="ts">
import { ref, computed, onMounted } from 'vue'
import axios from 'axios'
import { normalizeError } from '@/services/errorAdapter'

interface Token {
  token_prefix: string
  client_id: string
  user_id: string
  scope: string
  created_at: string
  expires_at: string
}

const tokens = ref<Token[]>([])
const loading = ref(true)
const page = ref(1)
const perPage = ref(50)
const total = ref(0)
const errorMessage = ref('')

// Filters
const clientIdFilter = ref('')
const userIdFilter = ref('')

// Bulk action dropdown
const showBulkMenu = ref(false)

// Confirmation dialog
const confirmDialog = ref(false)
const confirmAction = ref<(() => Promise<void>) | null>(null)
const confirmMessage = ref('')

const uniqueClientIds = computed(() => {
  const ids = new Set(tokens.value.map(t => t.client_id).filter(Boolean))
  return Array.from(ids)
})

async function fetchTokens() {
  loading.value = true
  errorMessage.value = ''
  try {
    const params: Record<string, string | number> = { page: page.value, per_page: perPage.value }
    if (clientIdFilter.value) params.client_id = clientIdFilter.value
    if (userIdFilter.value) params.user_id = userIdFilter.value

    const resp = await axios.get('/api/admin/tokens', { params })
    tokens.value = resp.data.tokens || []
    total.value = resp.data.total || 0
  } catch (e) {
    const normalized = normalizeError(e)
    errorMessage.value = normalized.message
    console.error('Failed to fetch tokens:', e)
  } finally {
    loading.value = false
  }
}

function applyFilters() {
  page.value = 1
  fetchTokens()
}

function clearFilters() {
  clientIdFilter.value = ''
  userIdFilter.value = ''
  page.value = 1
  fetchTokens()
}

function showConfirm(message: string, action: () => Promise<void>) {
  confirmMessage.value = message
  confirmAction.value = action
  confirmDialog.value = true
}

async function executeConfirm() {
  if (confirmAction.value) {
    await confirmAction.value()
  }
  confirmDialog.value = false
  confirmAction.value = null
}

function cancelConfirm() {
  confirmDialog.value = false
  confirmAction.value = null
}

async function revokeToken(tokenPrefix: string) {
  showConfirm(`Revoke token starting with "${tokenPrefix}"?`, async () => {
    try {
      await axios.delete(`/api/admin/tokens/${tokenPrefix}`)
      await fetchTokens()
    } catch (e) {
      const normalized = normalizeError(e)
      errorMessage.value = normalized.message
      console.error('Failed to revoke token:', e)
    }
  })
}

async function revokeByClient(clientId: string) {
  showBulkMenu.value = false
  showConfirm(`Revoke ALL tokens for client "${clientId}"?`, async () => {
    try {
      await axios.post('/api/admin/tokens/revoke-by-client', { client_id: clientId })
      await fetchTokens()
    } catch (e) {
      const normalized = normalizeError(e)
      errorMessage.value = normalized.message
      console.error('Failed to revoke tokens by client:', e)
    }
  })
}

async function revokeByUser() {
  if (!userIdFilter.value) return
  showConfirm(`Revoke ALL tokens for user "${userIdFilter.value}"?`, async () => {
    try {
      await axios.post('/api/admin/tokens/revoke-by-user', { user_id: userIdFilter.value })
      await fetchTokens()
    } catch (e) {
      const normalized = normalizeError(e)
      errorMessage.value = normalized.message
      console.error('Failed to revoke tokens by user:', e)
    }
  })
}

function formatTime(ts: string) {
  if (!ts) return '—'
  // The backend returns expires_at/created_at as a Unix-epoch-seconds string
  // (e.g. "1751464800"); a numeric string parsed by new Date(string) yields
  // "Invalid Date". Detect epoch-seconds and convert to milliseconds. ISO 8601
  // strings (logs) pass through unchanged. See A-TOK-012.
  const asNum = Number(ts)
  if (!Number.isNaN(asNum) && /^-?\d+$/.test(ts.trim())) {
    try { return new Date(asNum * 1000).toLocaleString() } catch { return ts }
  }
  try { return new Date(ts).toLocaleString() } catch { return ts }
}

onMounted(fetchTokens)
</script>

<template>
  <div>
    <div class="flex justify-between items-center mb-6">
      <h2 class="text-2xl font-bold text-gray-900">Tokens</h2>
      <div class="relative">
        <button
          @click="showBulkMenu = !showBulkMenu"
          class="inline-flex items-center px-4 py-2 border border-gray-300 rounded-md shadow-sm text-sm font-medium text-gray-700 bg-white hover:bg-gray-50"
        >
          Revoke All by App ▾
        </button>
        <div
          v-if="showBulkMenu"
          class="absolute right-0 mt-2 w-56 rounded-md shadow-lg bg-white ring-1 ring-black ring-opacity-5 z-10"
        >
          <div class="py-1">
            <button
              v-for="cid in uniqueClientIds"
              :key="cid"
              @click="revokeByClient(cid)"
              class="block w-full text-left px-4 py-2 text-sm text-gray-700 hover:bg-gray-100"
            >
              {{ cid }}
            </button>
            <p v-if="uniqueClientIds.length === 0" class="px-4 py-2 text-sm text-gray-400">
              No clients in current results
            </p>
          </div>
        </div>
      </div>
    </div>

    <!-- Error Banner -->
    <div v-if="errorMessage" class="mb-6 rounded-md bg-red-50 p-4">
      <div class="flex">
        <div class="flex-shrink-0">
          <svg class="h-5 w-5 text-red-400" viewBox="0 0 20 20" fill="currentColor">
            <path fill-rule="evenodd" d="M10 18a8 8 0 100-16 8 8 0 000 16zM8.28 7.22a.75.75 0 00-1.06 1.06L8.94 10l-1.72 1.72a.75.75 0 101.06 1.06L10 11.06l1.72 1.72a.75.75 0 101.06-1.06L11.06 10l1.72-1.72a.75.75 0 00-1.06-1.06L10 8.94 8.28 7.22z" clip-rule="evenodd" />
          </svg>
        </div>
        <div class="ml-3">
          <p class="text-sm text-red-800">{{ errorMessage }}</p>
        </div>
      </div>
    </div>

    <!-- Filter bar -->
    <div class="bg-white shadow rounded-lg p-4 mb-4 flex items-center gap-4 flex-wrap">
      <div class="flex items-center gap-2">
        <label class="text-sm font-medium text-gray-700">Client ID:</label>
        <input
          v-model="clientIdFilter"
          type="text"
          placeholder="Filter by client_id"
          class="border border-gray-300 rounded-md px-3 py-1.5 text-sm focus:ring-indigo-500 focus:border-indigo-500"
        />
      </div>
      <div class="flex items-center gap-2">
        <label class="text-sm font-medium text-gray-700">User ID:</label>
        <input
          v-model="userIdFilter"
          type="text"
          placeholder="Filter by user_id"
          class="border border-gray-300 rounded-md px-3 py-1.5 text-sm focus:ring-indigo-500 focus:border-indigo-500"
        />
      </div>
      <button
        @click="applyFilters"
        class="px-4 py-1.5 bg-indigo-600 text-white text-sm font-medium rounded-md hover:bg-indigo-700"
      >
        Apply
      </button>
      <button
        @click="clearFilters"
        class="px-4 py-1.5 border border-gray-300 text-gray-700 text-sm font-medium rounded-md hover:bg-gray-50"
      >
        Clear
      </button>
      <button
        v-if="userIdFilter"
        @click="revokeByUser"
        class="px-4 py-1.5 bg-red-600 text-white text-sm font-medium rounded-md hover:bg-red-700 ml-auto"
      >
        Revoke All for User
      </button>
    </div>

    <!-- Loading state -->
    <div v-if="loading" class="text-center py-12 text-gray-500">Loading...</div>

    <!-- Empty state -->
    <div v-else-if="tokens.length === 0" class="text-center py-12">
      <p class="text-gray-500">No active tokens found</p>
    </div>

    <!-- Token table -->
    <div v-else class="bg-white shadow rounded-lg overflow-hidden">
      <table class="min-w-full divide-y divide-gray-200">
        <thead class="bg-gray-50">
          <tr>
            <th class="px-4 py-3 text-left text-xs font-medium text-gray-500 uppercase">Token</th>
            <th class="px-4 py-3 text-left text-xs font-medium text-gray-500 uppercase">Type</th>
            <th class="px-4 py-3 text-left text-xs font-medium text-gray-500 uppercase">Client</th>
            <th class="px-4 py-3 text-left text-xs font-medium text-gray-500 uppercase">User</th>
            <th class="px-4 py-3 text-left text-xs font-medium text-gray-500 uppercase">Scope</th>
            <th class="px-4 py-3 text-left text-xs font-medium text-gray-500 uppercase">Expires</th>
            <th class="px-4 py-3 text-left text-xs font-medium text-gray-500 uppercase">Actions</th>
          </tr>
        </thead>
        <tbody class="bg-white divide-y divide-gray-200">
          <tr v-for="token in tokens" :key="token.token_prefix" class="hover:bg-gray-50">
            <td class="px-4 py-3 text-sm font-mono text-gray-900">{{ token.token_prefix }}</td>
            <td class="px-4 py-3 text-sm text-gray-500">access</td>
            <td class="px-4 py-3 text-sm text-gray-500">{{ token.client_id || '—' }}</td>
            <td class="px-4 py-3 text-sm text-gray-500">{{ token.user_id || '—' }}</td>
            <td class="px-4 py-3 text-xs text-gray-500 max-w-[200px] truncate">{{ token.scope || '—' }}</td>
            <td class="px-4 py-3 text-xs text-gray-500 whitespace-nowrap">{{ formatTime(token.expires_at) }}</td>
            <td class="px-4 py-3">
              <button
                @click="revokeToken(token.token_prefix)"
                class="text-sm text-red-600 hover:text-red-800 font-medium"
              >
                Revoke
              </button>
            </td>
          </tr>
        </tbody>
      </table>

      <!-- Pagination -->
      <div class="px-4 py-3 border-t flex justify-between items-center">
        <button
          @click="page > 1 && (page--, fetchTokens())"
          :disabled="page <= 1"
          class="text-sm text-indigo-600 disabled:text-gray-400"
        >
          ← Previous
        </button>
        <span class="text-sm text-gray-500">Page {{ page }} · {{ total }} total</span>
        <button
          @click="page++; fetchTokens()"
          :disabled="tokens.length < perPage"
          class="text-sm text-indigo-600 disabled:text-gray-400"
        >
          Next →
        </button>
      </div>
    </div>

    <!-- Confirmation Dialog -->
    <div v-if="confirmDialog" class="fixed inset-0 z-50 flex items-center justify-center">
      <div class="fixed inset-0 bg-black bg-opacity-30" @click="cancelConfirm"></div>
      <div class="relative bg-white rounded-lg shadow-xl p-6 max-w-sm w-full mx-4">
        <h3 class="text-lg font-medium text-gray-900 mb-2">Confirm Action</h3>
        <p class="text-sm text-gray-600 mb-4">{{ confirmMessage }}</p>
        <div class="flex justify-end gap-3">
          <button
            @click="cancelConfirm"
            class="px-4 py-2 text-sm font-medium text-gray-700 border border-gray-300 rounded-md hover:bg-gray-50"
          >
            Cancel
          </button>
          <button
            @click="executeConfirm"
            class="px-4 py-2 text-sm font-medium text-white bg-red-600 rounded-md hover:bg-red-700"
          >
            Confirm
          </button>
        </div>
      </div>
    </div>
  </div>
</template>
