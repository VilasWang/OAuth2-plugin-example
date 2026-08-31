<script setup lang="ts">
import { ref, onMounted } from 'vue'
import axios from 'axios'
import { normalizeError } from '../../services/errorAdapter'

const roles = ref<any[]>([])
const loading = ref(true)
const showCreateModal = ref(false)
const showEditModal = ref(false)
const selectedRole = ref<any>(null)
const saving = ref(false)
const successMessage = ref('')
const errorMessage = ref('')

const newRoleName = ref('')
const newRoleDescription = ref('')
const editDescription = ref('')

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

async function fetchRoles() {
  loading.value = true
  try {
    const resp = await axios.get('/api/admin/roles')
    roles.value = resp.data.roles || []
  } catch (e: unknown) {
    showError(normalizeError(e).message)
  } finally {
    loading.value = false
  }
}

async function createRole() {
  if (!newRoleName.value.trim()) return
  saving.value = true
  try {
    await axios.post('/api/admin/roles', {
      name: newRoleName.value.trim(),
      description: newRoleDescription.value.trim(),
    }, { headers: { 'Content-Type': 'application/json' } })
    showSuccess(`Role "${newRoleName.value}" created`)
    showCreateModal.value = false
    newRoleName.value = ''
    newRoleDescription.value = ''
    await fetchRoles()
  } catch (e: unknown) {
    showError(normalizeError(e).message)
  } finally {
    saving.value = false
  }
}

function openEditModal(role: any) {
  selectedRole.value = role
  editDescription.value = role.description || ''
  showEditModal.value = true
}

async function updateRole() {
  if (!selectedRole.value) return
  saving.value = true
  try {
    await axios.put(`/api/admin/roles/${selectedRole.value.id}`, {
      description: editDescription.value,
    }, { headers: { 'Content-Type': 'application/json' } })
    showSuccess('Role updated')
    showEditModal.value = false
    await fetchRoles()
  } catch (e: unknown) {
    showError(normalizeError(e).message)
  } finally {
    saving.value = false
  }
}

async function deleteRole(role: any) {
  if (!confirm(`Delete role "${role.name}"? This cannot be undone.`)) return
  try {
    await axios.delete(`/api/admin/roles/${role.id}`)
    showSuccess(`Role "${role.name}" deleted`)
    await fetchRoles()
  } catch (e: unknown) {
    showError(normalizeError(e).message)
  }
}

const BUILTIN_ROLES = ['admin', 'user']

onMounted(fetchRoles)
</script>

<template>
  <div>
    <div class="flex justify-between items-center mb-6">
      <h2 class="text-2xl font-bold text-neutral-900">
        Roles
      </h2>
      <button
        class="px-4 py-2 bg-brand-600 text-white rounded-md text-sm hover:bg-brand-700"
        @click="showCreateModal = true"
      >
        + Create Role
      </button>
    </div>

    <div
      v-if="successMessage"
      class="mb-4 p-3 bg-success-50 border border-success-200 text-success-700 rounded-md text-sm"
    >
      {{ successMessage }}
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
              Description
            </th>
            <th class="px-6 py-3 text-left text-xs font-medium text-neutral-500 uppercase">
              Users
            </th>
            <th class="px-6 py-3 text-left text-xs font-medium text-neutral-500 uppercase">
              Actions
            </th>
          </tr>
        </thead>
        <tbody class="bg-surface divide-y divide-neutral-200">
          <tr
            v-for="role in roles"
            :key="role.id"
            class="hover:bg-neutral-50"
          >
            <td class="px-6 py-4">
              <div class="flex items-center gap-2">
                <span class="text-sm font-medium text-neutral-900">{{ role.name }}</span>
                <span
                  v-if="BUILTIN_ROLES.includes(role.name)"
                  class="px-1.5 py-0.5 text-xs bg-neutral-100 text-neutral-500 rounded"
                >built-in</span>
              </div>
            </td>
            <td class="px-6 py-4 text-sm text-neutral-500">
              {{ role.description || '—' }}
            </td>
            <td class="px-6 py-4 text-sm text-neutral-500">
              {{ role.user_count }}
            </td>
            <td class="px-6 py-4 text-sm space-x-3">
              <button
                class="text-brand-600 hover:text-brand-900 transition-colors"
                @click="openEditModal(role)"
              >
                Edit
              </button>
              <button
                v-if="!BUILTIN_ROLES.includes(role.name)"
                class="text-error-600 hover:text-error-700 transition-colors"
                @click="deleteRole(role)"
              >
                Delete
              </button>
            </td>
          </tr>
          <tr v-if="roles.length === 0">
            <td
              colspan="4"
              class="px-6 py-12 text-center text-neutral-500"
            >
              No roles found
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
          Create Role
        </h3>
        <div class="space-y-4">
          <div>
            <label class="block text-sm font-medium text-neutral-700 mb-1">Name <span class="text-error-500">*</span></label>
            <input
              v-model="newRoleName"
              class="block w-full px-3 py-2 border border-neutral-300 rounded-md text-sm"
              placeholder="e.g. editor"
            >
          </div>
          <div>
            <label class="block text-sm font-medium text-neutral-700 mb-1">Description</label>
            <input
              v-model="newRoleDescription"
              class="block w-full px-3 py-2 border border-neutral-300 rounded-md text-sm"
              placeholder="Optional description"
            >
          </div>
        </div>
        <div class="flex justify-end gap-3 mt-6">
          <button
            class="px-4 py-2 border border-neutral-300 rounded-md text-sm"
            @click="showCreateModal = false"
          >
            Cancel
          </button>
          <button
            :disabled="saving || !newRoleName.trim()"
            class="px-4 py-2 bg-brand-600 text-white rounded-md text-sm hover:bg-brand-700 disabled:opacity-50"
            @click="createRole"
          >
            {{ saving ? 'Creating...' : 'Create' }}
          </button>
        </div>
      </div>
    </div>

    <!-- Edit Modal -->
    <div
      v-if="showEditModal"
      class="fixed inset-0 bg-black/50 flex items-center justify-center z-50"
    >
      <div class="bg-surface rounded-lg shadow-xl p-6 w-full max-w-md">
        <h3 class="text-lg font-semibold mb-4">
          Edit Role: {{ selectedRole?.name }}
        </h3>
        <div>
          <label class="block text-sm font-medium text-neutral-700 mb-1">Description</label>
          <input
            v-model="editDescription"
            class="block w-full px-3 py-2 border border-neutral-300 rounded-md text-sm"
            placeholder="Optional description"
          >
        </div>
        <div class="flex justify-end gap-3 mt-6">
          <button
            class="px-4 py-2 border border-neutral-300 rounded-md text-sm"
            @click="showEditModal = false"
          >
            Cancel
          </button>
          <button
            :disabled="saving"
            class="px-4 py-2 bg-brand-600 text-white rounded-md text-sm hover:bg-brand-700 disabled:opacity-50"
            @click="updateRole"
          >
            {{ saving ? 'Saving...' : 'Save' }}
          </button>
        </div>
      </div>
    </div>
  </div>
</template>
