<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { useAuthStore } from '../../stores/auth'
import http from '../../services/http'
import { normalizeError } from '../../services/errorAdapter'

const auth = useAuthStore()
const profile = ref<any>(null)
const loading = ref(true)
const success = ref('')
const error = ref('')

async function fetchProfile() {
  loading.value = true
  try {
    const resp = await http.get('/api/me')
    profile.value = resp.data
  } catch (e: any) {
    error.value = 'Failed to load profile'
  } finally {
    loading.value = false
  }
}

async function resendVerification() {
  try {
    await http.post('/api/verify-email/resend')
    success.value = 'Verification email sent!'
    setTimeout(() => { success.value = '' }, 3000)
  } catch (e: unknown) {
    error.value = normalizeError(e).message
  }
}

onMounted(fetchProfile)
</script>

<template>
  <div>
    <h1 class="text-2xl font-bold text-gray-900 mb-6">Profile</h1>

    <div v-if="success" class="mb-4 p-3 bg-green-50 border border-green-200 text-green-700 rounded-lg text-sm">{{ success }}</div>
    <div v-if="error" class="mb-4 p-3 bg-red-50 border border-red-200 text-red-700 rounded-lg text-sm">{{ error }}</div>

    <div v-if="loading" class="text-center py-12 text-gray-500">Loading...</div>

    <div v-else class="bg-white rounded-xl shadow-sm border border-gray-200 p-6 space-y-6">
      <div class="grid grid-cols-1 md:grid-cols-2 gap-6">
        <div>
          <label class="block text-sm font-medium text-gray-500">Username</label>
          <p class="mt-1 text-lg font-medium text-gray-900">{{ profile?.username || auth.user?.name }}</p>
        </div>
        <div>
          <label class="block text-sm font-medium text-gray-500">Account ID (sub)</label>
          <p class="mt-1 text-sm font-mono text-gray-700 break-all">{{ auth.user?.sub }}</p>
        </div>
        <div>
          <label class="block text-sm font-medium text-gray-500">Email</label>
          <div class="flex items-center gap-2 mt-1">
            <p class="text-gray-900">{{ profile?.email || 'N/A' }}</p>
            <span v-if="profile?.email_verified" class="px-2 py-0.5 text-xs bg-green-100 text-green-700 rounded-full">Verified</span>
            <span v-else class="px-2 py-0.5 text-xs bg-yellow-100 text-yellow-700 rounded-full">Unverified</span>
          </div>
          <button v-if="profile?.email && !profile?.email_verified" @click="resendVerification"
            class="mt-2 text-sm text-indigo-600 hover:text-indigo-800">
            Resend verification email
          </button>
        </div>
        <div>
          <label class="block text-sm font-medium text-gray-500">Roles</label>
          <div class="flex flex-wrap gap-1 mt-1">
            <span v-for="role in (auth.user?.roles || [])" :key="role"
              class="px-2 py-0.5 text-xs font-medium rounded-full bg-indigo-100 text-indigo-700">{{ role }}</span>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

