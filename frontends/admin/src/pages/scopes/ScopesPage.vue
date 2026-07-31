<script setup lang="ts">
import { ref, onMounted } from 'vue'
import axios from 'axios'
import { normalizeError } from '../../services/errorAdapter'

const scopes = ref<any[]>([])
const loading = ref(true)
const showCreateModal = ref(false)
const showEditModal = ref(false)
const selectedScope = ref<any>(null)
const saving = ref(false)
const successMessage = ref('')
const errorMessage = ref('')

const newScope = ref({ name: '', description: '', mapped_role: '', is_default: false, requires_admin_role: false })
const editScope = ref({ description: '', mapped_role: '', is_default: false, requires_admin_role: false })

function showSuccess(msg: string) {
  successMessage.value = msg
  errorMessage.value = ''
  setTimeout(() => { successMessage.value = '' }, 3000)
}
function showError(msg: string) {
  errorMessage.value = msg
  successMessage.value = ''
  setTimeout(() => { errorMessage.value = '' }, 5000)
}

async function fetchScopes() {
  loading.value = true
  try {
    const resp = await axios.get('/api/admin/scopes')
    scopes.value = resp.data.scopes || []
  } catch (e: unknown) {
    showError(normalizeError(e).message)
  } finally {
    loading.value = false
  }
}

async function createScope() {
  if (!newScope.value.name.trim()) return
  saving.value = true
  try {
    await axios.post('/api/admin/scopes', newScope.value, { headers: { 'Content-Type': 'application/json' } })
    showSuccess(`Scope "${newScope.value.name}" created`)
    showCreateModal.value = false
    newScope.value = { name: '', description: '', mapped_role: '', is_default: false, requires_admin_role: false }
    await fetchScopes()
  } catch (e: unknown) {
    showError(normalizeError(e).message)
  } finally {
    saving.value = false
  }
}

function openEditModal(scope: any) {
  selectedScope.value = scope
  editScope.value = {
    description: scope.description || '',
    mapped_role: scope.mapped_role || '',
    is_default: scope.is_default || false,
    requires_admin_role: scope.requires_admin_role || false,
  }
  showEditModal.value = true
}

async function updateScope() {
  if (!selectedScope.value) return
  saving.value = true
  try {
    await axios.put(`/api/admin/scopes/${selectedScope.value.id}`, editScope.value, { headers: { 'Content-Type': 'application/json' } })
    showSuccess('Scope updated')
    showEditModal.value = false
    await fetchScopes()
  } catch (e: unknown) {
    showError(normalizeError(e).message)
  } finally {
    saving.value = false
  }
}

async function deleteScope(scope: any) {
  if (!confirm(`Delete scope "${scope.name}"? This cannot be undone.`)) return
  try {
    await axios.delete(`/api/admin/scopes/${scope.id}`)
    showSuccess(`Scope "${scope.name}" deleted`)
    await fetchScopes()
  } catch (e: unknown) {
    showError(normalizeError(e).message)
  }
}

const BUILTIN_SCOPES = ['openid', 'profile', 'email', 'admin']

onMounted(fetchScopes)
</script>

<template>
  <div>
    <div class="flex justify-between items-center mb-6">
      <h2 class="text-2xl font-bold text-gray-900">Scopes</h2>
      <button @click="showCreateModal = true" class="px-4 py-2 bg-indigo-600 text-white rounded-md text-sm hover:bg-indigo-700">
        + Create Scope
      </button>
    </div>

    <div v-if="successMessage" class="mb-4 p-3 bg-green-50 border border-green-200 text-green-700 rounded-md text-sm">{{ successMessage }}</div>
    <div v-if="errorMessage" class="mb-4 p-3 bg-red-50 border border-red-200 text-red-700 rounded-md text-sm">{{ errorMessage }}</div>

    <div v-if="loading" class="text-center py-12 text-gray-500">Loading...</div>

    <div v-else class="bg-white shadow rounded-lg overflow-hidden">
      <table class="min-w-full divide-y divide-gray-200">
        <thead class="bg-gray-50">
          <tr>
            <th class="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase">Name</th>
            <th class="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase">Description</th>
            <th class="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase">Mapped Role</th>
            <th class="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase">Flags</th>
            <th class="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase">Actions</th>
          </tr>
        </thead>
        <tbody class="bg-white divide-y divide-gray-200">
          <tr v-for="scope in scopes" :key="scope.id" class="hover:bg-gray-50">
            <td class="px-6 py-4">
              <div class="flex items-center gap-2">
                <span class="text-sm font-medium text-gray-900 font-mono">{{ scope.name }}</span>
                <span v-if="BUILTIN_SCOPES.includes(scope.name)" class="px-1.5 py-0.5 text-xs bg-gray-100 text-gray-500 rounded">built-in</span>
              </div>
            </td>
            <td class="px-6 py-4 text-sm text-gray-500">{{ scope.description || '—' }}</td>
            <td class="px-6 py-4 text-sm text-gray-500">{{ scope.mapped_role || '—' }}</td>
            <td class="px-6 py-4">
              <div class="flex gap-1 flex-wrap">
                <span v-if="scope.is_default" class="px-1.5 py-0.5 text-xs bg-blue-100 text-blue-700 rounded">default</span>
                <span v-if="scope.requires_admin_role" class="px-1.5 py-0.5 text-xs bg-orange-100 text-orange-700 rounded">admin only</span>
              </div>
            </td>
            <td class="px-6 py-4 text-sm space-x-3">
              <button @click="openEditModal(scope)" class="text-indigo-600 hover:text-indigo-900 transition-colors">Edit</button>
              <button v-if="!BUILTIN_SCOPES.includes(scope.name)" @click="deleteScope(scope)" class="text-red-600 hover:text-red-900 transition-colors">Delete</button>
            </td>
          </tr>
          <tr v-if="scopes.length === 0">
            <td colspan="5" class="px-6 py-12 text-center text-gray-500">No scopes found</td>
          </tr>
        </tbody>
      </table>
    </div>

    <!-- Create Modal -->
    <div v-if="showCreateModal" class="fixed inset-0 bg-black/50 flex items-center justify-center z-50">
      <div class="bg-white rounded-lg shadow-xl p-6 w-full max-w-md">
        <h3 class="text-lg font-semibold mb-4">Create Scope</h3>
        <div class="space-y-4">
          <div>
            <label class="block text-sm font-medium text-gray-700 mb-1">Name <span class="text-red-500">*</span></label>
            <input v-model="newScope.name" class="block w-full px-3 py-2 border border-gray-300 rounded-md text-sm font-mono" placeholder="e.g. reports:read" />
          </div>
          <div>
            <label class="block text-sm font-medium text-gray-700 mb-1">Description</label>
            <input v-model="newScope.description" class="block w-full px-3 py-2 border border-gray-300 rounded-md text-sm" placeholder="What this scope grants access to" />
          </div>
          <div>
            <label class="block text-sm font-medium text-gray-700 mb-1">Mapped Role</label>
            <input v-model="newScope.mapped_role" class="block w-full px-3 py-2 border border-gray-300 rounded-md text-sm" placeholder="e.g. user" />
          </div>
          <div class="flex gap-6">
            <label class="flex items-center gap-2 cursor-pointer">
              <input type="checkbox" v-model="newScope.is_default" class="h-4 w-4 rounded border-gray-300 text-indigo-600" />
              <span class="text-sm text-gray-700">Default scope</span>
            </label>
            <label class="flex items-center gap-2 cursor-pointer">
              <input type="checkbox" v-model="newScope.requires_admin_role" class="h-4 w-4 rounded border-gray-300 text-indigo-600" />
              <span class="text-sm text-gray-700">Requires admin role</span>
            </label>
          </div>
        </div>
        <div class="flex justify-end gap-3 mt-6">
          <button @click="showCreateModal = false" class="px-4 py-2 border border-gray-300 rounded-md text-sm">Cancel</button>
          <button @click="createScope" :disabled="saving || !newScope.name.trim()" class="px-4 py-2 bg-indigo-600 text-white rounded-md text-sm hover:bg-indigo-700 disabled:opacity-50">
            {{ saving ? 'Creating...' : 'Create' }}
          </button>
        </div>
      </div>
    </div>

    <!-- Edit Modal -->
    <div v-if="showEditModal" class="fixed inset-0 bg-black/50 flex items-center justify-center z-50">
      <div class="bg-white rounded-lg shadow-xl p-6 w-full max-w-md">
        <h3 class="text-lg font-semibold mb-4">Edit Scope: <code class="font-mono text-indigo-600">{{ selectedScope?.name }}</code></h3>
        <div class="space-y-4">
          <div>
            <label class="block text-sm font-medium text-gray-700 mb-1">Description</label>
            <input v-model="editScope.description" class="block w-full px-3 py-2 border border-gray-300 rounded-md text-sm" />
          </div>
          <div>
            <label class="block text-sm font-medium text-gray-700 mb-1">Mapped Role</label>
            <input v-model="editScope.mapped_role" class="block w-full px-3 py-2 border border-gray-300 rounded-md text-sm" />
          </div>
          <div class="flex gap-6">
            <label class="flex items-center gap-2 cursor-pointer">
              <input type="checkbox" v-model="editScope.is_default" class="h-4 w-4 rounded border-gray-300 text-indigo-600" />
              <span class="text-sm text-gray-700">Default scope</span>
            </label>
            <label class="flex items-center gap-2 cursor-pointer">
              <input type="checkbox" v-model="editScope.requires_admin_role" class="h-4 w-4 rounded border-gray-300 text-indigo-600" />
              <span class="text-sm text-gray-700">Requires admin role</span>
            </label>
          </div>
        </div>
        <div class="flex justify-end gap-3 mt-6">
          <button @click="showEditModal = false" class="px-4 py-2 border border-gray-300 rounded-md text-sm">Cancel</button>
          <button @click="updateScope" :disabled="saving" class="px-4 py-2 bg-indigo-600 text-white rounded-md text-sm hover:bg-indigo-700 disabled:opacity-50">
            {{ saving ? 'Saving...' : 'Save' }}
          </button>
        </div>
      </div>
    </div>
  </div>
</template>
