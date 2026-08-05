<script setup lang="ts">
import { ref, onMounted } from 'vue'
import axios from 'axios'
import { normalizeError } from '../../services/errorAdapter'
import AppAlert from '../../components/ui/AppAlert.vue'
import AppSkeleton from '../../components/ui/AppSkeleton.vue'

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
  <div class="space-y-8">
    <!-- Page header -->
    <div>
      <h2 class="text-2xl font-bold text-neutral-900 tracking-tight">Dashboard</h2>
      <p class="mt-1 text-sm text-neutral-500">System overview and key metrics for your OAuth2 platform</p>
    </div>

    <!-- Error -->
    <AppAlert v-if="errorMessage" type="error" dismissible @dismiss="errorMessage = ''">
      {{ errorMessage }}
    </AppAlert>

    <!-- Stats Grid -->
    <div v-if="loading">
      <AppSkeleton type="card" :count="4" />
    </div>

    <div v-else class="grid grid-cols-2 lg:grid-cols-4 gap-5">
      <!-- Users -->
      <div class="bg-white rounded-xl border border-neutral-200 p-5 hover:border-sky-300 hover:shadow-sm transition-all duration-150">
        <div class="flex items-center justify-between mb-3">
          <span class="text-xs font-semibold text-neutral-500 uppercase tracking-wider">Total Users</span>
          <div class="w-9 h-9 rounded-lg bg-sky-50 flex items-center justify-center">
            <svg class="w-5 h-5 text-sky-600" viewBox="0 0 20 20" fill="currentColor">
              <path d="M7 10a3 3 0 100-6 3 3 0 000 6zM3.5 11.5A3.5 3.5 0 000 15v1.25a.75.75 0 001.5 0V15a2 2 0 012-2h7a2 2 0 012 2v1.25a.75.75 0 001.5 0V15a3.5 3.5 0 00-3.5-3.5h-7z"/>
            </svg>
          </div>
        </div>
        <p class="text-3xl font-bold text-neutral-900 tracking-tight">{{ stats?.total_users ?? 0 }}</p>
        <p class="mt-1 text-xs text-neutral-400">Registered accounts</p>
      </div>

      <!-- Applications -->
      <div class="bg-white rounded-xl border border-neutral-200 p-5 hover:border-sky-300 hover:shadow-sm transition-all duration-150">
        <div class="flex items-center justify-between mb-3">
          <span class="text-xs font-semibold text-neutral-500 uppercase tracking-wider">Applications</span>
          <div class="w-9 h-9 rounded-lg bg-emerald-50 flex items-center justify-center">
            <svg class="w-5 h-5 text-emerald-600" viewBox="0 0 20 20" fill="currentColor">
              <path fill-rule="evenodd" d="M2.75 4.5A2.25 2.25 0 015 2.25h2.5A2.25 2.25 0 019.75 4.5v2.5A2.25 2.25 0 017.5 9.25H5a2.25 2.25 0 01-2.25-2.25v-2.5zM5 3.75a.75.75 0 00-.75.75v2.5c0 .414.336.75.75.75h2.5a.75.75 0 00.75-.75v-2.5a.75.75 0 00-.75-.75H5zm6 6a.75.75 0 01.75-.75h5.5a.75.75 0 010 1.5h-5.5a.75.75 0 01-.75-.75zm.75 3.25a.75.75 0 100 1.5h5.5a.75.75 0 100-1.5h-5.5zm0 4a.75.75 0 100 1.5h5.5a.75.75 0 100-1.5h-5.5zM5.75 9a.75.75 0 01.75.75v5.5a.75.75 0 01-1.5 0v-5.5a.75.75 0 01.75-.75z"/>
            </svg>
          </div>
        </div>
        <p class="text-3xl font-bold text-neutral-900 tracking-tight">{{ stats?.total_clients ?? 0 }}</p>
        <p class="mt-1 text-xs text-neutral-400">OAuth2 clients</p>
      </div>

      <!-- Active Tokens -->
      <div class="bg-white rounded-xl border border-neutral-200 p-5 hover:border-sky-300 hover:shadow-sm transition-all duration-150">
        <div class="flex items-center justify-between mb-3">
          <span class="text-xs font-semibold text-neutral-500 uppercase tracking-wider">Active Tokens</span>
          <div class="w-9 h-9 rounded-lg bg-amber-50 flex items-center justify-center">
            <svg class="w-5 h-5 text-amber-600" viewBox="0 0 20 20" fill="currentColor">
              <path fill-rule="evenodd" d="M10 2a4 4 0 00-4 4v4H4a2 2 0 00-2 2v4a2 2 0 002 2h12a2 2 0 002-2v-4a2 2 0 00-2-2h-2V6a4 4 0 00-4-4zm1.5 6.5a1 1 0 11-2 0 1 1 0 012 0zM4 14h12v4H4v-4z"/>
            </svg>
          </div>
        </div>
        <p class="text-3xl font-bold text-amber-600 tracking-tight">{{ stats?.active_tokens ?? 0 }}</p>
        <p class="mt-1 text-xs text-neutral-400">Issued &amp; valid</p>
      </div>

      <!-- Failures Today -->
      <div class="bg-white rounded-xl border border-neutral-200 p-5 hover:border-neutral-300 hover:shadow-sm transition-all duration-150"
           :class="(stats?.failures_today || 0) > 0 ? 'border-rose-200' : ''">
        <div class="flex items-center justify-between mb-3">
          <span class="text-xs font-semibold text-neutral-500 uppercase tracking-wider">Failures Today</span>
          <div class="w-9 h-9 rounded-lg flex items-center justify-center"
               :class="(stats?.failures_today || 0) > 0 ? 'bg-rose-50' : 'bg-neutral-50'">
            <svg class="w-5 h-5" :class="(stats?.failures_today || 0) > 0 ? 'text-rose-600' : 'text-neutral-400'" viewBox="0 0 20 20" fill="currentColor">
              <path fill-rule="evenodd" d="M18 10a8 8 0 11-16 0 8 8 0 0116 0zm-8-5a.75.75 0 01.75.75v4.5a.75.75 0 01-1.5 0v-4.5A.75.75 0 0110 5zm0 10a1 1 0 100-2 1 1 0 000 2z"/>
            </svg>
          </div>
        </div>
        <p class="text-3xl font-bold tracking-tight"
           :class="(stats?.failures_today || 0) > 0 ? 'text-rose-600' : 'text-neutral-900'">
          {{ stats?.failures_today ?? 0 }}
        </p>
        <p class="mt-1 text-xs text-neutral-400">Failed auth attempts</p>
      </div>
    </div>

    <!-- Health + Quick Actions -->
    <div class="grid grid-cols-1 lg:grid-cols-3 gap-5">
      <!-- System Health -->
      <div class="lg:col-span-2 bg-white rounded-xl border border-neutral-200 shadow-sm overflow-hidden">
        <div class="px-6 py-4 border-b border-neutral-100">
          <h2 class="text-sm font-semibold text-neutral-900">System Health</h2>
        </div>

        <div v-if="loading" class="p-6">
          <AppSkeleton type="text" :count="3" />
        </div>

        <div v-else class="divide-y divide-neutral-100">
          <div class="flex items-center justify-between px-6 py-4">
            <div class="flex items-center gap-3">
              <div class="w-2 h-2 rounded-full" :class="health?.status === 'ok' ? 'bg-emerald-500' : 'bg-rose-500'" />
              <span class="text-sm font-medium text-neutral-700">System Status</span>
            </div>
            <span class="text-sm font-medium" :class="health?.status === 'ok' ? 'text-emerald-600' : 'text-rose-600'">
              {{ health?.status === 'ok' ? 'Healthy' : 'Unhealthy' }}
            </span>
          </div>

          <div class="flex items-center justify-between px-6 py-4">
            <span class="text-sm text-neutral-600">Database</span>
            <div class="flex items-center gap-2">
              <div class="w-1.5 h-1.5 rounded-full bg-emerald-500" />
              <span class="text-sm font-medium text-neutral-700">{{ health?.database || 'Connected' }}</span>
            </div>
          </div>

          <div class="flex items-center justify-between px-6 py-4">
            <span class="text-sm text-neutral-600">Redis</span>
            <div class="flex items-center gap-2">
              <div class="w-1.5 h-1.5 rounded-full bg-emerald-500" />
              <span class="text-sm font-medium text-neutral-700">{{ health?.redis || 'Connected' }}</span>
            </div>
          </div>
        </div>
      </div>

      <!-- Quick Actions -->
      <div class="bg-white rounded-xl border border-neutral-200 shadow-sm">
        <div class="px-6 py-4 border-b border-neutral-100">
          <h2 class="text-sm font-semibold text-neutral-900">Quick Actions</h2>
        </div>

        <div class="p-4 space-y-1">
          <router-link to="/applications" class="flex items-center gap-3 px-3 py-2.5 rounded-lg text-sm text-neutral-700 hover:bg-sky-50 hover:text-sky-700 transition-colors group">
            <svg class="w-4 h-4 text-neutral-400 group-hover:text-sky-500" viewBox="0 0 20 20" fill="currentColor">
              <path fill-rule="evenodd" d="M2.75 4.5A2.25 2.25 0 015 2.25h2.5A2.25 2.25 0 019.75 4.5v2.5A2.25 2.25 0 017.5 9.25H5a2.25 2.25 0 01-2.25-2.25v-2.5z"/>
            </svg>
            Manage Applications
          </router-link>

          <router-link to="/users" class="flex items-center gap-3 px-3 py-2.5 rounded-lg text-sm text-neutral-700 hover:bg-sky-50 hover:text-sky-700 transition-colors group">
            <svg class="w-4 h-4 text-neutral-400 group-hover:text-sky-500" viewBox="0 0 20 20" fill="currentColor">
              <path d="M7 10a3 3 0 100-6 3 3 0 000 6z"/>
            </svg>
            Manage Users
          </router-link>

          <router-link to="/roles" class="flex items-center gap-3 px-3 py-2.5 rounded-lg text-sm text-neutral-700 hover:bg-sky-50 hover:text-sky-700 transition-colors group">
            <svg class="w-4 h-4 text-neutral-400 group-hover:text-sky-500" viewBox="0 0 20 20" fill="currentColor">
              <path fill-rule="evenodd" d="M10 3a1.5 1.5 0 00-1.5 1.5A1.5 1.5 0 007 4.5 1.5 1.5 0 005.5 6 1.5 1.5 0 007 7.5 1.5 1.5 0 008.5 6a1.5 1.5 0 001.5-1.5A1.5 1.5 0 0010 3z"/>
            </svg>
            Manage Roles
          </router-link>

          <router-link to="/scopes" class="flex items-center gap-3 px-3 py-2.5 rounded-lg text-sm text-neutral-700 hover:bg-sky-50 hover:text-sky-700 transition-colors group">
            <svg class="w-4 h-4 text-neutral-400 group-hover:text-sky-500" viewBox="0 0 20 20" fill="currentColor">
              <path fill-rule="evenodd" d="M8 2a2 2 0 00-2 2v1H4a2 2 0 00-2 2v9a2 2 0 002 2h12a2 2 0 002-2V7a2 2 0 00-2-2h-2V4a2 2 0 00-2-2H8z"/>
            </svg>
            Manage Scopes
          </router-link>

          <router-link to="/logs" class="flex items-center gap-3 px-3 py-2.5 rounded-lg text-sm text-neutral-700 hover:bg-sky-50 hover:text-sky-700 transition-colors group">
            <svg class="w-4 h-4 text-neutral-400 group-hover:text-sky-500" viewBox="0 0 20 20" fill="currentColor">
              <path fill-rule="evenodd" d="M4 3.5A1.5 1.5 0 015.5 2h9A1.5 1.5 0 0116 3.5v13a1.5 1.5 0 01-1.5 1.5h-9A1.5 1.5 0 014 16.5v-13z"/>
            </svg>
            View Audit Logs
          </router-link>
        </div>
      </div>
    </div>
  </div>
</template>
