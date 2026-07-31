<script setup lang="ts">
import { ref, onMounted } from 'vue'
import axios from 'axios'
import { normalizeError } from '@/services/errorAdapter'

const logs = ref<any[]>([])
const loading = ref(true)
const page = ref(1)
const errorMessage = ref('')

// Filters (A-LOG-004). action/outcome/actor_id are passed as query params; the
// backend applies them server-side.
const actionFilter = ref('')
const outcomeFilter = ref('')

async function fetchLogs() {
  loading.value = true
  errorMessage.value = ''
  try {
    const params: Record<string, string | number> = { page: page.value, per_page: 50 }
    if (actionFilter.value) params.action = actionFilter.value
    if (outcomeFilter.value) params.outcome = outcomeFilter.value
    const resp = await axios.get('/api/admin/logs', { params })
    logs.value = resp.data.logs || []
  } catch (e) {
    const normalized = normalizeError(e)
    errorMessage.value = normalized.message
    console.error('Failed to fetch logs:', e)
  } finally {
    loading.value = false
  }
}

function applyFilters() {
  page.value = 1
  fetchLogs()
}

function clearFilters() {
  actionFilter.value = ''
  outcomeFilter.value = ''
  page.value = 1
  fetchLogs()
}

function formatTime(ts: string) {
  if (!ts) return '—'
  try { return new Date(ts).toLocaleString() } catch { return ts }
}

onMounted(fetchLogs)
</script>

<template>
  <div>
    <h2 class="text-2xl font-bold text-gray-900 mb-6">Audit Logs</h2>

    <!-- Filter bar -->
    <div class="bg-white shadow rounded-lg p-4 mb-4 flex items-center gap-4 flex-wrap">
      <div class="flex items-center gap-2">
        <label class="text-sm font-medium text-gray-700">Action:</label>
        <input
          v-model="actionFilter"
          type="text"
          placeholder="e.g. login_success"
          class="border border-gray-300 rounded-md px-3 py-1.5 text-sm focus:ring-indigo-500 focus:border-indigo-500"
          @keyup.enter="applyFilters"
        />
      </div>
      <div class="flex items-center gap-2">
        <label class="text-sm font-medium text-gray-700">Outcome:</label>
        <select
          v-model="outcomeFilter"
          class="border border-gray-300 rounded-md px-3 py-1.5 text-sm focus:ring-indigo-500 focus:border-indigo-500"
        >
          <option value="">(any)</option>
          <option value="success">success</option>
          <option value="failure">failure</option>
        </select>
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

    <div v-if="loading" class="text-center py-12 text-gray-500">Loading...</div>

    <div v-else-if="logs.length === 0" class="text-center py-12">
      <p class="text-gray-500">No audit logs recorded yet</p>
    </div>

    <div v-else class="bg-white shadow rounded-lg overflow-hidden">
      <table class="min-w-full divide-y divide-gray-200">
        <thead class="bg-gray-50">
          <tr>
            <th class="px-4 py-3 text-left text-xs font-medium text-gray-500 uppercase">Time</th>
            <th class="px-4 py-3 text-left text-xs font-medium text-gray-500 uppercase">Action</th>
            <th class="px-4 py-3 text-left text-xs font-medium text-gray-500 uppercase">Actor</th>
            <th class="px-4 py-3 text-left text-xs font-medium text-gray-500 uppercase">Outcome</th>
            <th class="px-4 py-3 text-left text-xs font-medium text-gray-500 uppercase">IP</th>
          </tr>
        </thead>
        <tbody class="bg-white divide-y divide-gray-200">
          <tr v-for="log in logs" :key="log.id" class="hover:bg-gray-50">
            <td class="px-4 py-3 text-xs text-gray-500 whitespace-nowrap">{{ formatTime(log.timestamp) }}</td>
            <td class="px-4 py-3 text-sm font-medium text-gray-900">{{ log.action }}</td>
            <td class="px-4 py-3 text-xs text-gray-500">
              <span class="text-gray-400">{{ log.actor_type }}:</span> {{ log.actor_id?.substring(0, 12) || '—' }}
            </td>
            <td class="px-4 py-3">
              <span class="px-2 py-0.5 text-xs rounded-full" :class="log.outcome === 'success' ? 'bg-green-100 text-green-800' : 'bg-red-100 text-red-800'">
                {{ log.outcome }}
              </span>
            </td>
            <td class="px-4 py-3 text-xs text-gray-500 font-mono">{{ log.ip || '—' }}</td>
          </tr>
        </tbody>
      </table>

      <div class="px-4 py-3 border-t flex justify-between items-center">
        <button @click="page > 1 && (page--, fetchLogs())" :disabled="page <= 1" class="text-sm text-indigo-600 disabled:text-gray-400">← Previous</button>
        <span class="text-sm text-gray-500">Page {{ page }}</span>
        <button @click="page++; fetchLogs()" :disabled="logs.length < 50" class="text-sm text-indigo-600 disabled:text-gray-400">Next →</button>
      </div>
    </div>
  </div>
</template>
