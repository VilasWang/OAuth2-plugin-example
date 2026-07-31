<script setup lang="ts">
import { ref, onMounted } from 'vue'
import axios from 'axios'
import { normalizeError } from '@/services/errorAdapter'

const scopes = ref<any[]>([])
const loading = ref(true)
const errorMessage = ref('')

interface OidcKeyInfo {
  kid: string
  kty: string
  alg: string
  use: string
  jwks_uri: string
  discovery_uri: string
  key_status: string
  note: string
}

const oidcKeys = ref<OidcKeyInfo | null>(null)
const oidcLoading = ref(true)
const oidcErrorMessage = ref('')
const copySuccess = ref('')

async function fetchScopes() {
  loading.value = true
  errorMessage.value = ''
  try {
    const resp = await axios.get('/api/admin/scopes')
    scopes.value = resp.data.scopes || []
  } catch (e) {
    const normalized = normalizeError(e)
    errorMessage.value = normalized.message
    console.error('Failed to fetch scopes:', e)
  } finally {
    loading.value = false
  }
}

async function fetchOidcKeys() {
  oidcLoading.value = true
  oidcErrorMessage.value = ''
  try {
    const resp = await axios.get('/api/admin/oidc/keys')
    oidcKeys.value = resp.data
  } catch (e) {
    const normalized = normalizeError(e)
    oidcErrorMessage.value = normalized.message
    console.error('Failed to fetch OIDC keys:', e)
  } finally {
    oidcLoading.value = false
  }
}

async function copyToClipboard(text: string, label: string) {
  try {
    await navigator.clipboard.writeText(text)
    copySuccess.value = label
    setTimeout(() => {
      copySuccess.value = ''
    }, 2000)
  } catch (e) {
    console.error('Failed to copy:', e)
  }
}

onMounted(() => {
  fetchScopes()
  fetchOidcKeys()
})
</script>

<template>
  <div>
    <h2 class="text-2xl font-bold text-gray-900 mb-6">Settings & Scopes</h2>

    <!-- Error Banner for Scopes -->
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

    <!-- Scopes Section -->
    <div class="bg-white shadow rounded-lg overflow-hidden">
      <div class="px-6 py-4 border-b">
        <h3 class="text-lg font-medium text-gray-900">OAuth2 Scopes</h3>
      </div>

      <div v-if="loading" class="p-6 text-center text-gray-500">Loading...</div>

      <table v-else class="min-w-full divide-y divide-gray-200">
        <thead class="bg-gray-50">
          <tr>
            <th class="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase">Name</th>
            <th class="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase">Description</th>
            <th class="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase">Mapped Role</th>
            <th class="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase">Default</th>
            <th class="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase">Admin Only</th>
          </tr>
        </thead>
        <tbody class="bg-white divide-y divide-gray-200">
          <tr v-for="scope in scopes" :key="scope.id" class="hover:bg-gray-50">
            <td class="px-6 py-4 text-sm font-medium text-gray-900 font-mono">{{ scope.name }}</td>
            <td class="px-6 py-4 text-sm text-gray-500">{{ scope.description || '—' }}</td>
            <td class="px-6 py-4 text-sm text-gray-500">{{ scope.mapped_role || '—' }}</td>
            <td class="px-6 py-4">
              <span v-if="scope.is_default" class="text-green-600">✓</span>
              <span v-else class="text-gray-300">—</span>
            </td>
            <td class="px-6 py-4">
              <span v-if="scope.requires_admin_role" class="text-red-600">✓</span>
              <span v-else class="text-gray-300">—</span>
            </td>
          </tr>
        </tbody>
      </table>
    </div>

    <!-- OIDC Signing Keys Section -->
    <div class="bg-white shadow rounded-lg overflow-hidden mt-8">
      <div class="px-6 py-4 border-b">
        <h3 class="text-lg font-medium text-gray-900">OIDC Signing Keys</h3>
      </div>

      <!-- Error Banner for OIDC Keys -->
      <div v-if="oidcErrorMessage" class="m-6 rounded-md bg-red-50 p-4">
        <div class="flex">
          <div class="flex-shrink-0">
            <svg class="h-5 w-5 text-red-400" viewBox="0 0 20 20" fill="currentColor">
              <path fill-rule="evenodd" d="M10 18a8 8 0 100-16 8 8 0 000 16zM8.28 7.22a.75.75 0 00-1.06 1.06L8.94 10l-1.72 1.72a.75.75 0 101.06 1.06L10 11.06l1.72 1.72a.75.75 0 101.06-1.06L11.06 10l1.72-1.72a.75.75 0 00-1.06-1.06L10 8.94 8.28 7.22z" clip-rule="evenodd" />
            </svg>
          </div>
          <div class="ml-3">
            <p class="text-sm text-red-800">{{ oidcErrorMessage }}</p>
          </div>
        </div>
      </div>

      <div v-if="oidcLoading" class="p-6 text-center text-gray-500">Loading...</div>

      <div v-else-if="oidcKeys" class="p-6 space-y-6">
        <!-- Key Metadata -->
        <dl class="grid grid-cols-1 sm:grid-cols-2 gap-x-6 gap-y-4">
          <div>
            <dt class="text-sm font-medium text-gray-500">Key ID (kid)</dt>
            <dd class="mt-1 text-sm text-gray-900 font-mono">{{ oidcKeys.kid }}</dd>
          </div>
          <div>
            <dt class="text-sm font-medium text-gray-500">Key Type (kty)</dt>
            <dd class="mt-1 text-sm text-gray-900 font-mono">{{ oidcKeys.kty }}</dd>
          </div>
          <div>
            <dt class="text-sm font-medium text-gray-500">Algorithm (alg)</dt>
            <dd class="mt-1 text-sm text-gray-900 font-mono">{{ oidcKeys.alg }}</dd>
          </div>
          <div>
            <dt class="text-sm font-medium text-gray-500">Usage (use)</dt>
            <dd class="mt-1 text-sm text-gray-900 font-mono">{{ oidcKeys.use }}</dd>
          </div>
          <div>
            <dt class="text-sm font-medium text-gray-500">Status</dt>
            <dd class="mt-1">
              <span class="inline-flex items-center px-2.5 py-0.5 rounded-full text-xs font-medium bg-green-100 text-green-800">
                {{ oidcKeys.key_status }}
              </span>
            </dd>
          </div>
        </dl>

        <!-- URLs -->
        <div class="border-t pt-4 space-y-3">
          <div class="flex items-center justify-between">
            <div>
              <span class="text-sm font-medium text-gray-500">JWKS Endpoint</span>
              <p class="mt-1 text-sm text-gray-900 font-mono">{{ oidcKeys.jwks_uri }}</p>
            </div>
            <button
              @click="copyToClipboard(oidcKeys.jwks_uri, 'jwks')"
              class="ml-4 inline-flex items-center px-3 py-1.5 border border-gray-300 text-xs font-medium rounded text-gray-700 bg-white hover:bg-gray-50 focus:outline-none focus:ring-2 focus:ring-offset-2 focus:ring-indigo-500"
            >
              {{ copySuccess === 'jwks' ? 'Copied!' : 'Copy' }}
            </button>
          </div>
          <div class="flex items-center justify-between">
            <div>
              <span class="text-sm font-medium text-gray-500">Discovery Endpoint</span>
              <p class="mt-1 text-sm text-gray-900 font-mono">{{ oidcKeys.discovery_uri }}</p>
            </div>
            <button
              @click="copyToClipboard(oidcKeys.discovery_uri, 'discovery')"
              class="ml-4 inline-flex items-center px-3 py-1.5 border border-gray-300 text-xs font-medium rounded text-gray-700 bg-white hover:bg-gray-50 focus:outline-none focus:ring-2 focus:ring-offset-2 focus:ring-indigo-500"
            >
              {{ copySuccess === 'discovery' ? 'Copied!' : 'Copy' }}
            </button>
          </div>
        </div>

        <!-- Note -->
        <div class="border-t pt-4">
          <div class="rounded-md bg-blue-50 p-4">
            <div class="flex">
              <div class="flex-shrink-0">
                <svg class="h-5 w-5 text-blue-400" viewBox="0 0 20 20" fill="currentColor" aria-hidden="true">
                  <path fill-rule="evenodd" d="M18 10a8 8 0 11-16 0 8 8 0 0116 0zm-7-4a1 1 0 11-2 0 1 1 0 012 0zM9 9a.75.75 0 000 1.5h.253a.25.25 0 01.244.304l-.459 2.066A1.75 1.75 0 0010.747 15H11a.75.75 0 000-1.5h-.253a.25.25 0 01-.244-.304l.459-2.066A1.75 1.75 0 009.253 9H9z" clip-rule="evenodd" />
                </svg>
              </div>
              <div class="ml-3">
                <p class="text-sm text-blue-700">{{ oidcKeys.note }}</p>
              </div>
            </div>
          </div>
        </div>
      </div>

      <div v-else class="p-6 text-center text-gray-500">
        Failed to load OIDC key information.
      </div>
    </div>
  </div>
</template>
