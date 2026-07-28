<script setup lang="ts">
import { ref, onMounted } from 'vue'
import axios from 'axios'
import { normalizeError } from '@/services/errorAdapter'

const health = ref<any>(null)
const stats = ref<any>(null)
const loading = ref(true)
const errorMessage = ref('')

onMounted(async () => {
  try {
    const [healthResp, statsResp] = await Promise.all([
      axios.get('/health/ready'),
      axios.get('/api/admin/dashboard/stats'),
    ])
    health.value = healthResp.data
    stats.value = statsResp.data
  } catch (e) {
    const normalized = normalizeError(e)
    errorMessage.value = normalized.message
    health.value = { status: 'error' }
  } finally {
    loading.value = false
  }
})
</script>

<template>
  <div>
    <h2 class="text-2xl font-bold text-gray-900 mb-6">Dashboard</h2>

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

    <!-- Stats Grid -->
    <div class="grid grid-cols-2 md:grid-cols-4 gap-4 mb-8">
      <div class="bg-white rounded-lg shadow p-5">
        <p class="text-xs font-medium text-gray-500 uppercase">Total Users</p>
        <p class="mt-2 text-3xl font-bold text-gray-900">{{ loading ? '—' : (stats?.total_users ?? '—') }}</p>
      </div>
      <div class="bg-white rounded-lg shadow p-5">
        <p class="text-xs font-medium text-gray-500 uppercase">Applications</p>
        <p class="mt-2 text-3xl font-bold text-gray-900">{{ loading ? '—' : (stats?.total_clients ?? '—') }}</p>
      </div>
      <div class="bg-white rounded-lg shadow p-5">
        <p class="text-xs font-medium text-gray-500 uppercase">Active Tokens</p>
        <p class="mt-2 text-3xl font-bold text-indigo-600">{{ loading ? '—' : (stats?.active_tokens ?? '—') }}</p>
      </div>
      <div class="bg-white rounded-lg shadow p-5">
        <p class="text-xs font-medium text-gray-500 uppercase">Failures Today</p>
        <p class="mt-2 text-3xl font-bold" :class="(stats?.failures_today || 0) > 0 ? 'text-red-600' : 'text-gray-900'">
          {{ loading ? '—' : (stats?.failures_today ?? '—') }}
        </p>
      </div>
    </div>

    <!-- System Health -->
    <div class="grid grid-cols-1 md:grid-cols-3 gap-4 mb-8">
      <div class="bg-white rounded-lg shadow p-5">
        <div class="flex items-center gap-2">
          <div class="w-2.5 h-2.5 rounded-full" :class="health?.status === 'ok' ? 'bg-green-500' : 'bg-red-500'"></div>
          <h3 class="text-sm font-medium text-gray-500">System Status</h3>
        </div>
        <p class="mt-2 text-xl font-semibold" :class="health?.status === 'ok' ? 'text-green-600' : 'text-red-600'">
          {{ loading ? '...' : (health?.status === 'ok' ? 'Healthy' : 'Unhealthy') }}
        </p>
      </div>
      <div class="bg-white rounded-lg shadow p-5">
        <h3 class="text-sm font-medium text-gray-500">Database</h3>
        <p class="mt-2 text-xl font-semibold text-gray-900">{{ loading ? '...' : (health?.database || 'N/A') }}</p>
      </div>
      <div class="bg-white rounded-lg shadow p-5">
        <h3 class="text-sm font-medium text-gray-500">Redis</h3>
        <p class="mt-2 text-xl font-semibold text-gray-900">{{ loading ? '...' : (health?.redis || 'N/A') }}</p>
      </div>
    </div>

    <!-- Quick Links -->
    <div class="bg-white rounded-lg shadow p-6">
      <h3 class="text-lg font-medium text-gray-900 mb-4">Quick Actions</h3>
      <div class="grid grid-cols-2 md:grid-cols-4 gap-4">
        <router-link to="/applications" class="p-4 border rounded-lg hover:border-indigo-500 hover:bg-indigo-50 transition-colors text-center">
          <span class="text-2xl">📱</span>
          <p class="mt-2 text-sm font-medium text-gray-700">Applications</p>
        </router-link>
        <router-link to="/users" class="p-4 border rounded-lg hover:border-indigo-500 hover:bg-indigo-50 transition-colors text-center">
          <span class="text-2xl">👥</span>
          <p class="mt-2 text-sm font-medium text-gray-700">Users</p>
        </router-link>
        <router-link to="/roles" class="p-4 border rounded-lg hover:border-indigo-500 hover:bg-indigo-50 transition-colors text-center">
          <span class="text-2xl">🛡️</span>
          <p class="mt-2 text-sm font-medium text-gray-700">Roles</p>
        </router-link>
        <router-link to="/scopes" class="p-4 border rounded-lg hover:border-indigo-500 hover:bg-indigo-50 transition-colors text-center">
          <span class="text-2xl">🔐</span>
          <p class="mt-2 text-sm font-medium text-gray-700">Scopes</p>
        </router-link>
      </div>
    </div>
  </div>
</template>
