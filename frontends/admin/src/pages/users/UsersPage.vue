<script setup lang="ts">
import { ref, onMounted, computed } from 'vue'
import axios from 'axios'
import { normalizeError } from '../../services/errorAdapter'

const users = ref<any[]>([])
const loading = ref(true)
const showRoleModal = ref(false)
const showCreateModal = ref(false)
const selectedUser = ref<any>(null)
const roleInput = ref('')
const saving = ref(false)
const errorMessage = ref('')
const successMessage = ref('')

// Pagination + search/filter state
const currentPage = ref(1)
const perPage = ref(50)
const total = ref(0)
const totalPages = ref(0)
const searchQuery = ref('')
const roleFilter = ref('')
const lockedFilter = ref('')

// Create-user form state
const createForm = ref({ username: '', password: '', email: '', roles: '', email_verified: false, mfa_enabled: false })

const hasPrev = computed(() => currentPage.value > 1)
const hasNext = computed(() => currentPage.value < totalPages.value)

function showError(msg: string) {
  errorMessage.value = msg
  setTimeout(() => { errorMessage.value = '' }, 5000)
}
function showSuccess(msg: string) {
  successMessage.value = msg
  setTimeout(() => { successMessage.value = '' }, 3000)
}

async function fetchUsers() {
  loading.value = true
  try {
    const params: Record<string, string | number> = {
      page: currentPage.value,
      per_page: perPage.value,
    }
    if (searchQuery.value) params.q = searchQuery.value
    if (roleFilter.value) params.role = roleFilter.value
    if (lockedFilter.value) params.locked = lockedFilter.value
    const resp = await axios.get('/api/admin/users', { params })
    users.value = resp.data.users || []
    total.value = resp.data.total || 0
    totalPages.value = resp.data.total_pages || 0
  } catch (e: unknown) {
    showError(normalizeError(e).message)
  } finally {
    loading.value = false
  }
}

function applySearch() {
  currentPage.value = 1
  fetchUsers()
}

function goToPage(page: number) {
  if (page < 1 || page > totalPages.value) return
  currentPage.value = page
  fetchUsers()
}

function openRoleModal(user: any) {
  selectedUser.value = user
  roleInput.value = (user.roles || []).join(', ')
  showRoleModal.value = true
}

async function assignRoles() {
  if (!selectedUser.value || !roleInput.value.trim()) return
  saving.value = true
  try {
    const roles = roleInput.value.split(',').map((r: string) => r.trim()).filter(Boolean)
    await axios.put(`/api/admin/users/${selectedUser.value.id}/roles`, { roles }, {
      headers: { 'Content-Type': 'application/json' },
    })
    showRoleModal.value = false
    showSuccess('Roles assigned successfully')
    await fetchUsers()
  } catch (e: unknown) {
    showError(normalizeError(e).message)
  } finally {
    saving.value = false
  }
}

async function createUser() {
  if (!createForm.value.username.trim() || !createForm.value.password.trim()) {
    showError('Username and password are required')
    return
  }
  if (createForm.value.password.length < 8) {
    showError('Password must be at least 8 characters')
    return
  }
  saving.value = true
  try {
    const body: any = {
      username: createForm.value.username,
      password: createForm.value.password,
      email_verified: createForm.value.email_verified,
      mfa_enabled: createForm.value.mfa_enabled,
    }
    if (createForm.value.email) body.email = createForm.value.email
    if (createForm.value.roles) {
      body.roles = createForm.value.roles.split(',').map((r: string) => r.trim()).filter(Boolean)
    }
    const resp = await axios.post('/api/admin/users', body, { headers: { 'Content-Type': 'application/json' } })
    showCreateModal.value = false
    createForm.value = { username: '', password: '', email: '', roles: '', email_verified: false, mfa_enabled: false }
    // Gap-fix: surface partial failures instead of always reporting success —
    // the create response carries roles_failed/warning when role assignment
    // failed for some entries (UserAdminService).
    const failedRoles: string[] = resp.data?.roles_failed || []
    if (failedRoles.length > 0 || resp.data?.warning) {
      showError(`User created, but role assignment failed for: ${failedRoles.join(', ')}${resp.data?.warning ? ` — ${resp.data.warning}` : ''}`)
    } else {
      showSuccess('User created successfully')
    }
    await fetchUsers()
  } catch (e: unknown) {
    showError(normalizeError(e).message)
  } finally {
    saving.value = false
  }
}

async function deleteUser(user: any) {
  if (!confirm(`Delete user "${user.username}"? This action cannot be undone.`)) return
  try {
    const resp = await axios.delete(`/api/admin/users/${user.id}`)
    // Gap-fix: the delete response reports whether tokens were revoked; a
    // tokens_revoked=false + warning means some tokens may outlive the user
    // row — surface it instead of a blanket success.
    if (resp.data?.tokens_revoked === false) {
      showError(`User deleted, but token revocation reported: ${resp.data?.warning || 'some tokens could not be revoked'}`)
    } else {
      showSuccess('User deleted successfully')
    }
    await fetchUsers()
  } catch (e: unknown) {
    showError(normalizeError(e).message)
  }
}

onMounted(fetchUsers)
</script>

<template>
  <div>
    <div class="flex items-center justify-between mb-6">
      <h2 class="text-2xl font-bold text-neutral-900">
        Users
      </h2>
      <button
        class="px-4 py-2 bg-brand-600 text-white rounded-md text-sm hover:bg-brand-700"
        @click="showCreateModal = true"
      >
        + Create User
      </button>
    </div>

    <div
      v-if="errorMessage"
      class="mb-4 p-3 bg-error-50 border border-error-200 text-error-700 rounded-md text-sm"
    >
      {{ errorMessage }}
    </div>
    <div
      v-if="successMessage"
      class="mb-4 p-3 bg-green-50 border border-green-200 text-green-700 rounded-md text-sm"
    >
      {{ successMessage }}
    </div>

    <!-- Search + filter bar -->
    <div class="mb-4 flex flex-wrap items-center gap-3">
      <input
        v-model="searchQuery"
        class="px-3 py-2 border border-neutral-300 rounded-md text-sm w-64"
        placeholder="Search username or email..."
        @keyup.enter="applySearch"
      >
      <select
        v-model="roleFilter"
        class="px-3 py-2 border border-neutral-300 rounded-md text-sm"
        @change="applySearch"
      >
        <option value="">
          All roles
        </option>
        <option value="admin">
          admin
        </option>
        <option value="user">
          user
        </option>
      </select>
      <select
        v-model="lockedFilter"
        class="px-3 py-2 border border-neutral-300 rounded-md text-sm"
        @change="applySearch"
      >
        <option value="">
          All status
        </option>
        <option value="true">
          Locked
        </option>
        <option value="false">
          Active
        </option>
      </select>
      <button
        class="px-4 py-2 bg-neutral-100 text-neutral-700 rounded-md text-sm hover:bg-neutral-200"
        @click="applySearch"
      >
        Search
      </button>
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
              ID
            </th>
            <th class="px-6 py-3 text-left text-xs font-medium text-neutral-500 uppercase">
              Username
            </th>
            <th class="px-6 py-3 text-left text-xs font-medium text-neutral-500 uppercase">
              Email
            </th>
            <th class="px-6 py-3 text-left text-xs font-medium text-neutral-500 uppercase">
              Verified
            </th>
            <th class="px-6 py-3 text-left text-xs font-medium text-neutral-500 uppercase">
              MFA
            </th>
            <th class="px-6 py-3 text-left text-xs font-medium text-neutral-500 uppercase">
              Actions
            </th>
          </tr>
        </thead>
        <tbody class="bg-surface divide-y divide-neutral-200">
          <tr
            v-for="user in users"
            :key="user.id"
            class="hover:bg-neutral-50"
          >
            <td class="px-6 py-4 text-sm text-neutral-400">
              {{ user.id }}
            </td>
            <td class="px-6 py-4 text-sm font-medium text-neutral-900">
              {{ user.username }}
            </td>
            <td class="px-6 py-4 text-sm text-neutral-500">
              {{ user.email || '—' }}
            </td>
            <td class="px-6 py-4">
              <span
                class="px-2 py-1 text-xs rounded-full"
                :class="user.email_verified ? 'bg-green-100 text-green-800' : 'bg-yellow-100 text-yellow-800'"
              >
                {{ user.email_verified ? 'Verified' : 'Pending' }}
              </span>
            </td>
            <td class="px-6 py-4">
              <span
                class="px-2 py-1 text-xs rounded-full"
                :class="user.mfa_enabled ? 'bg-green-100 text-green-800' : 'bg-neutral-100 text-neutral-600'"
              >
                {{ user.mfa_enabled ? 'Enabled' : 'Off' }}
              </span>
            </td>
            <td class="px-6 py-4 text-sm">
              <button
                class="text-brand-600 hover:text-brand-900 mr-3"
                @click="openRoleModal(user)"
              >
                Assign Roles
              </button>
              <router-link
                :to="{ name: 'user-detail', params: { id: user.id } }"
                class="text-neutral-600 hover:text-neutral-900 mr-3"
              >
                Details
              </router-link>
              <button
                class="text-error-600 hover:text-error-700"
                @click="deleteUser(user)"
              >
                Delete
              </button>
            </td>
          </tr>
        </tbody>
      </table>

      <!-- Pagination controls -->
      <div
        v-if="totalPages > 1"
        class="px-6 py-3 bg-neutral-50 flex items-center justify-between border-t border-neutral-200"
      >
        <span class="text-sm text-neutral-600">
          {{ total }} user(s) · Page {{ currentPage }} of {{ totalPages }}
        </span>
        <div class="flex gap-2">
          <button
            :disabled="!hasPrev"
            class="px-3 py-1.5 border border-neutral-300 rounded-md text-sm disabled:opacity-50 disabled:cursor-not-allowed hover:bg-neutral-100"
            @click="goToPage(currentPage - 1)"
          >
            Previous
          </button>
          <button
            :disabled="!hasNext"
            class="px-3 py-1.5 border border-neutral-300 rounded-md text-sm disabled:opacity-50 disabled:cursor-not-allowed hover:bg-neutral-100"
            @click="goToPage(currentPage + 1)"
          >
            Next
          </button>
        </div>
      </div>
    </div>

    <!-- Create User Modal -->
    <div
      v-if="showCreateModal"
      class="fixed inset-0 bg-black/50 flex items-center justify-center z-50"
    >
      <div class="bg-surface rounded-lg shadow-xl p-6 w-full max-w-md">
        <h3 class="text-lg font-semibold mb-4">
          Create User
        </h3>
        <div class="space-y-4">
          <div>
            <label class="block text-sm font-medium text-neutral-700">Username *</label>
            <input
              v-model="createForm.username"
              class="mt-1 block w-full px-3 py-2 border border-neutral-300 rounded-md text-sm"
              placeholder="newuser"
            >
          </div>
          <div>
            <label class="block text-sm font-medium text-neutral-700">Password *</label>
            <input
              v-model="createForm.password"
              type="password"
              class="mt-1 block w-full px-3 py-2 border border-neutral-300 rounded-md text-sm"
              placeholder="••••••••"
            >
          </div>
          <div>
            <label class="block text-sm font-medium text-neutral-700">Email</label>
            <input
              v-model="createForm.email"
              type="email"
              class="mt-1 block w-full px-3 py-2 border border-neutral-300 rounded-md text-sm"
              placeholder="user@example.com"
            >
          </div>
          <div>
            <label class="block text-sm font-medium text-neutral-700">Roles (comma-separated)</label>
            <input
              v-model="createForm.roles"
              class="mt-1 block w-full px-3 py-2 border border-neutral-300 rounded-md text-sm"
              placeholder="user"
            >
            <p class="mt-1 text-xs text-neutral-500">
              Default: user. Available: admin, user
            </p>
          </div>
          <div class="flex gap-6">
            <label class="flex items-center gap-2 text-sm text-neutral-700">
              <input
                v-model="createForm.email_verified"
                type="checkbox"
                class="rounded border-neutral-300 text-brand-600"
              >
              Email verified
            </label>
            <label class="flex items-center gap-2 text-sm text-neutral-700">
              <input
                v-model="createForm.mfa_enabled"
                type="checkbox"
                class="rounded border-neutral-300 text-brand-600"
              >
              MFA enabled
            </label>
          </div>
        </div>
        <div class="flex justify-end space-x-3 mt-6">
          <button
            class="px-4 py-2 border border-neutral-300 rounded-md text-sm"
            @click="showCreateModal = false"
          >
            Cancel
          </button>
          <button
            :disabled="saving"
            class="px-4 py-2 bg-brand-600 text-white rounded-md text-sm hover:bg-brand-700 disabled:opacity-50"
            @click="createUser"
          >
            {{ saving ? 'Creating...' : 'Create User' }}
          </button>
        </div>
      </div>
    </div>

    <!-- Role Assignment Modal -->
    <div
      v-if="showRoleModal"
      class="fixed inset-0 bg-black/50 flex items-center justify-center z-50"
    >
      <div class="bg-surface rounded-lg shadow-xl p-6 w-full max-w-md">
        <h3 class="text-lg font-semibold mb-2">
          Assign Roles
        </h3>
        <p class="text-sm text-neutral-600 mb-4">
          User: <strong>{{ selectedUser?.username }}</strong>
        </p>
        <div>
          <label class="block text-sm font-medium text-neutral-700">Roles (comma-separated)</label>
          <input
            v-model="roleInput"
            class="mt-1 block w-full px-3 py-2 border border-neutral-300 rounded-md text-sm"
            placeholder="admin, user"
          >
          <p class="mt-1 text-xs text-neutral-500">
            Available: admin, user
          </p>
        </div>
        <div class="flex justify-end space-x-3 mt-4">
          <button
            class="px-4 py-2 border border-neutral-300 rounded-md text-sm"
            @click="showRoleModal = false"
          >
            Cancel
          </button>
          <button
            :disabled="saving"
            class="px-4 py-2 bg-brand-600 text-white rounded-md text-sm hover:bg-brand-700 disabled:opacity-50"
            @click="assignRoles"
          >
            {{ saving ? 'Saving...' : 'Save Roles' }}
          </button>
        </div>
      </div>
    </div>
  </div>
</template>
