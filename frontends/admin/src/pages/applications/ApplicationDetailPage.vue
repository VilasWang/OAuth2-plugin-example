<script setup lang="ts">
import { ref, onMounted, computed } from 'vue'
import { useRoute } from 'vue-router'
import axios from 'axios'
import { normalizeError } from '../../services/errorAdapter'

const route = useRoute()
const clientId = computed(() => route.params.id as string)

const loading = ref(true)
const saving = ref(false)
const savingScopes = ref(false)
const activeTab = ref<'info' | 'auth' | 'scopes' | 'credentials'>('info')
const successMessage = ref('')
const errorMessage = ref('')

// Client data
const client = ref<any>({})
const editName = ref('')
const editRedirectUris = ref('')
const editGrantTypes = ref<string[]>([])
const editBackchannelLogoutUri = ref('')

// Scopes data
const allScopes = ref<any[]>([])
const clientScopes = ref<string[]>([])

// Secret modal
const showSecretModal = ref(false)
const newClientSecret = ref('')

const AVAILABLE_GRANT_TYPES = [
  { value: 'authorization_code', label: 'Authorization Code', description: '标准授权码流程（推荐）' },
  { value: 'refresh_token', label: 'Refresh Token', description: '允许刷新访问令牌' },
  { value: 'client_credentials', label: 'Client Credentials', description: '服务间通信（M2M）' },
  { value: 'urn:ietf:params:oauth:grant-type:device_code', label: 'Device Code', description: '无浏览器设备授权' },
]

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

async function fetchClient() {
  loading.value = true
  try {
    const resp = await axios.get(`/api/admin/clients/${clientId.value}`)
    client.value = resp.data
    editName.value = resp.data.name || ''
    editRedirectUris.value = (resp.data.redirect_uris || '').replace(/,/g, '\n')
    editGrantTypes.value = resp.data.allowed_grant_types
      ? resp.data.allowed_grant_types.split(',').filter((s: string) => s)
      : []
    editBackchannelLogoutUri.value = resp.data.backchannel_logout_uri || ''
    clientScopes.value = resp.data.scopes || []
  } catch (e: unknown) {
    showError(normalizeError(e).message)
  } finally {
    loading.value = false
  }
}

async function fetchAllScopes() {
  try {
    const resp = await axios.get('/api/admin/scopes')
    allScopes.value = resp.data.scopes || []
  } catch (e) {
    console.error('Failed to fetch scopes:', e)
  }
}

async function saveChanges() {
  saving.value = true
  try {
    const body: any = {}
    if (editName.value !== (client.value.name || '')) {
      // RFC 7591 §2.2: client_name is required; the backend now rejects
      // empty names with 400. Validate client-side for better UX.
      if (!editName.value.trim()) {
        showError('应用名称不能为空')
        saving.value = false
        return
      }
      body.name = editName.value
    }
    const uris = editRedirectUris.value.split('\n').map(s => s.trim()).filter(s => s).join(',')
    if (uris !== (client.value.redirect_uris || '')) {
      body.redirect_uris = uris
    }
    const grants = editGrantTypes.value.join(',')
    if (grants !== (client.value.allowed_grant_types || '')) {
      body.allowed_grant_types = grants
    }
    if (editBackchannelLogoutUri.value !== (client.value.backchannel_logout_uri || '')) {
      body.backchannel_logout_uri = editBackchannelLogoutUri.value
    }

    if (Object.keys(body).length === 0) {
      showSuccess('No changes to save')
      saving.value = false
      return
    }

    await axios.put(`/api/admin/clients/${clientId.value}`, body, {
      headers: { 'Content-Type': 'application/json' },
    })
    showSuccess('Changes saved successfully')
    await fetchClient()
  } catch (e: unknown) {
    showError(normalizeError(e).message)
  } finally {
    saving.value = false
  }
}

async function saveScopes() {
  savingScopes.value = true
  try {
    const resp = await axios.put(`/api/admin/clients/${clientId.value}/scopes`, {
      scopes: clientScopes.value,
    }, {
      headers: { 'Content-Type': 'application/json' },
    })
    clientScopes.value = resp.data.scopes || clientScopes.value
    showSuccess('Scopes updated successfully')
  } catch (e: unknown) {
    showError(normalizeError(e).message)
  } finally {
    savingScopes.value = false
  }
}

async function resetSecret() {
  if (!confirm(`Reset secret for "${clientId.value}"? The old secret will be immediately invalidated.`)) return
  try {
    const resp = await axios.post(`/api/admin/clients/${clientId.value}/reset-secret`)
    newClientSecret.value = resp.data.client_secret || ''
    showSecretModal.value = true
  } catch (e: unknown) {
    showError(normalizeError(e).message)
  }
}

function copyToClipboard(text: string) {
  navigator.clipboard.writeText(text)
  showSuccess('Copied to clipboard')
}

onMounted(() => {
  fetchClient()
  fetchAllScopes()
})
</script>

<template>
  <div>
    <!-- Header -->
    <div class="flex justify-between items-center mb-6">
      <div class="flex items-center gap-3">
        <router-link
          :to="{ name: 'applications' }"
          class="text-neutral-500 hover:text-neutral-700 text-sm"
        >
          ← Back to Applications
        </router-link>
      </div>
      <button
        v-if="activeTab === 'info' || activeTab === 'auth'"
        :disabled="saving"
        class="px-4 py-2 bg-brand-600 text-white rounded-md hover:bg-brand-700 text-sm font-medium disabled:opacity-50"
        @click="saveChanges"
      >
        {{ saving ? 'Saving...' : 'Save Changes' }}
      </button>
    </div>

    <!-- Toast Messages -->
    <div
      v-if="successMessage"
      class="mb-4 p-3 bg-green-50 border border-green-200 text-green-700 rounded-md text-sm"
    >
      {{ successMessage }}
    </div>
    <div
      v-if="errorMessage"
      class="mb-4 p-3 bg-error-50 border border-error-200 text-error-700 rounded-md text-sm"
    >
      {{ errorMessage }}
    </div>

    <!-- Loading -->
    <div
      v-if="loading"
      class="text-center py-12 text-neutral-500"
    >
      Loading...
    </div>

    <!-- Content -->
    <div v-else>
      <!-- App Title -->
      <h2 class="text-2xl font-bold text-neutral-900 mb-6">
        {{ client.name || client.client_id }}
      </h2>

      <!-- Tabs -->
      <div class="border-b border-neutral-200 mb-6">
        <nav class="-mb-px flex space-x-8">
          <button
            v-for="tab in [
              { key: 'info', label: 'Info' },
              { key: 'auth', label: 'Auth Config' },
              { key: 'scopes', label: 'Scopes' },
              { key: 'credentials', label: 'Credentials' },
            ]"
            :key="tab.key"
            :class="[
              'py-3 px-1 border-b-2 text-sm font-medium transition-colors',
              activeTab === tab.key
                ? 'border-brand-500 text-brand-600'
                : 'border-transparent text-neutral-500 hover:text-neutral-700 hover:border-neutral-300'
            ]"
            @click="activeTab = tab.key as any"
          >
            {{ tab.label }}
          </button>
        </nav>
      </div>

      <!-- Info Tab -->
      <div
        v-if="activeTab === 'info'"
        class="bg-surface shadow rounded-lg p-6 space-y-5"
      >
        <div>
          <label class="block text-sm font-medium text-neutral-700 mb-1">Client ID</label>
          <div class="flex items-center gap-2">
            <code class="flex-1 px-3 py-2 bg-neutral-100 rounded-md text-sm font-mono text-neutral-800">{{ client.client_id }}</code>
            <button
              class="px-3 py-2 text-sm text-brand-600 hover:bg-brand-50 rounded-md transition-colors"
              @click="copyToClipboard(client.client_id)"
            >
              Copy
            </button>
          </div>
        </div>
        <div>
          <label class="block text-sm font-medium text-neutral-700 mb-1">Name</label>
          <input
            v-model="editName"
            class="block w-full px-3 py-2 border border-neutral-300 rounded-md text-sm focus:ring-brand-500 focus:border-brand-500"
            placeholder="Application name"
          >
        </div>
        <div>
          <label class="block text-sm font-medium text-neutral-700 mb-1">Type</label>
          <span
            class="px-3 py-1 text-sm rounded-full"
            :class="client.client_type === 'PUBLIC' ? 'bg-brand-100 text-brand-800' : 'bg-purple-100 text-purple-800'"
          >
            {{ client.client_type }}
          </span>
        </div>
      </div>

      <!-- Auth Config Tab -->
      <div
        v-if="activeTab === 'auth'"
        class="bg-surface shadow rounded-lg p-6 space-y-5"
      >
        <div>
          <label class="block text-sm font-medium text-neutral-700 mb-1">Token Endpoint Auth Method</label>
          <div class="px-3 py-2 bg-neutral-50 rounded-md text-sm font-mono text-neutral-700 inline-block">
            {{ client.token_endpoint_auth_method || '(not set)' }}
          </div>
          <p class="text-xs text-neutral-500 mt-1">
            How this client authenticates at the token endpoint. PUBLIC clients use <code>none</code>;
            CONFIDENTIAL clients use <code>client_secret_basic</code> or <code>client_secret_post</code>.
            Set automatically based on client type.
          </p>
        </div>
        <div>
          <label class="block text-sm font-medium text-neutral-700 mb-1">Redirect URIs (one per line)</label>
          <textarea
            v-model="editRedirectUris"
            rows="4"
            class="block w-full px-3 py-2 border border-neutral-300 rounded-md text-sm font-mono focus:ring-brand-500 focus:border-brand-500"
            placeholder="https://myapp.com/callback"
          />
        </div>
        <div>
          <label class="block text-sm font-medium text-neutral-700 mb-2">Allowed Grant Types</label>
          <div class="space-y-2">
            <label
              v-for="gt in AVAILABLE_GRANT_TYPES"
              :key="gt.value"
              class="flex items-start gap-2 cursor-pointer"
            >
              <input
                v-model="editGrantTypes"
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
        <div>
          <label class="block text-sm font-medium text-neutral-700 mb-1">Backchannel Logout URI</label>
          <input
            v-model="editBackchannelLogoutUri"
            class="block w-full px-3 py-2 border border-neutral-300 rounded-md text-sm font-mono focus:ring-brand-500 focus:border-brand-500"
            placeholder="https://rp.example.com/backchannel-logout"
          >
          <p class="text-xs text-neutral-500 mt-1">
            OIDC Back-Channel Logout 1.0. When set (https), the OP POSTs a signed
            logout_token here on user logout. Leave empty to disable.
          </p>
        </div>
      </div>

      <!-- Scopes Tab -->
      <div
        v-if="activeTab === 'scopes'"
        class="bg-surface shadow rounded-lg p-6"
      >
        <div
          v-if="editGrantTypes.includes('client_credentials')"
          class="mb-4 p-3 bg-brand-50 border border-brand-200 text-brand-700 rounded-md text-sm"
        >
          这些 Scope 决定了该应用通过 Client Credentials 模式获取的 Token 权限范围
        </div>
        <div
          v-if="allScopes.length === 0"
          class="text-neutral-500 text-sm"
        >
          No scopes available in the system.
        </div>
        <div
          v-else
          class="space-y-3"
        >
          <label
            v-for="scope in allScopes"
            :key="scope.name"
            class="flex items-start gap-3 cursor-pointer p-2 rounded hover:bg-neutral-50"
          >
            <input
              v-model="clientScopes"
              type="checkbox"
              :value="scope.name"
              class="mt-0.5 h-4 w-4 rounded border-neutral-300 text-brand-600 focus:ring-brand-500"
            >
            <div>
              <span class="text-sm font-medium text-neutral-700">{{ scope.name }}</span>
              <p
                v-if="scope.description"
                class="text-xs text-neutral-500"
              >{{ scope.description }}</p>
              <p
                v-if="scope.requires_admin_role"
                class="text-xs text-orange-600"
              >Requires admin role</p>
            </div>
          </label>
        </div>
        <div class="mt-6 pt-4 border-t border-neutral-200">
          <button
            :disabled="savingScopes"
            class="px-4 py-2 bg-brand-600 text-white rounded-md hover:bg-brand-700 text-sm font-medium disabled:opacity-50"
            @click="saveScopes"
          >
            {{ savingScopes ? 'Saving...' : 'Save Scopes' }}
          </button>
        </div>
      </div>

      <!-- Credentials Tab -->
      <div
        v-if="activeTab === 'credentials'"
        class="bg-surface shadow rounded-lg p-6"
      >
        <div v-if="client.client_type === 'CONFIDENTIAL'">
          <h3 class="text-sm font-medium text-neutral-700 mb-2">
            Client Secret
          </h3>
          <p class="text-sm text-neutral-500 mb-4">
            The client secret is stored securely and cannot be viewed. You can reset it to generate a new one.
          </p>
          <button
            class="px-4 py-2 bg-error-600 text-white rounded-md hover:bg-error-700 text-sm font-medium"
            @click="resetSecret"
          >
            Reset Client Secret
          </button>
        </div>
        <div v-else>
          <p class="text-sm text-neutral-500">
            Public clients do not have a client secret.
          </p>
        </div>
      </div>
    </div>

    <!-- Secret Display Modal -->
    <div
      v-if="showSecretModal"
      class="fixed inset-0 bg-black/50 flex items-center justify-center z-50"
    >
      <div class="bg-surface rounded-lg shadow-xl p-6 w-full max-w-md">
        <h3 class="text-lg font-semibold mb-2">
          New Client Secret
        </h3>
        <p class="text-sm text-error-600 mb-4">
          Copy this secret now. It will not be shown again.
        </p>
        <div class="bg-neutral-100 p-3 rounded-md font-mono text-sm break-all select-all">
          {{ newClientSecret }}
        </div>
        <div class="flex justify-end mt-4 gap-2">
          <button
            class="px-4 py-2 border border-neutral-300 rounded-md text-sm hover:bg-neutral-50"
            @click="copyToClipboard(newClientSecret)"
          >
            Copy
          </button>
          <button
            class="px-4 py-2 bg-brand-600 text-white rounded-md text-sm hover:bg-brand-700"
            @click="showSecretModal = false; newClientSecret = ''"
          >
            Done
          </button>
        </div>
      </div>
    </div>
  </div>
</template>
