<script setup lang="ts">
import { useAuthStore } from '../../stores/auth'
import { useRouter } from 'vue-router'

const auth = useAuthStore()
const router = useRouter()

const navigation = [
  { name: 'Dashboard', path: '/', icon: '📊' },
  { name: 'Applications', path: '/applications', icon: '📱' },
  { name: 'Users', path: '/users', icon: '👥' },
  { name: 'Roles', path: '/roles', icon: '🛡️' },
  { name: 'Scopes', path: '/scopes', icon: '🔐' },
  { name: 'Tokens', path: '/tokens', icon: '🔑' },
  { name: 'Audit Logs', path: '/logs', icon: '📋' },
  { name: 'Settings', path: '/settings', icon: '⚙️' },
]

function handleLogout() {
  auth.logout()
  router.push('/login')
}
</script>

<template>
  <div class="flex h-screen bg-gray-50">
    <!-- Sidebar -->
    <aside class="w-64 bg-gray-900 text-white flex flex-col">
      <!-- Logo -->
      <div class="h-16 flex items-center px-6 border-b border-gray-800">
        <span class="text-xl font-bold">OAuth2 Admin</span>
      </div>

      <!-- Navigation -->
      <nav class="flex-1 px-3 py-4 space-y-1">
        <router-link
          v-for="item in navigation"
          :key="item.path"
          :to="item.path"
          class="flex items-center px-3 py-2 rounded-md text-sm font-medium transition-colors"
          :class="($route.path === item.path || (item.path !== '/' && $route.path.startsWith(item.path)))
            ? 'bg-gray-800 text-white'
            : 'text-gray-300 hover:bg-gray-800 hover:text-white'"
        >
          <span class="mr-3">{{ item.icon }}</span>
          {{ item.name }}
        </router-link>
      </nav>

      <!-- User info -->
      <div class="p-4 border-t border-gray-800">
        <div class="flex items-center">
          <div class="w-8 h-8 rounded-full bg-indigo-600 flex items-center justify-center text-sm font-bold">
            {{ auth.user?.name?.[0]?.toUpperCase() || 'A' }}
          </div>
          <div class="ml-3 flex-1 min-w-0">
            <p class="text-sm font-medium text-white truncate">{{ auth.user?.name || 'Admin' }}</p>
            <p class="text-xs text-gray-400 truncate">{{ auth.user?.email || '' }}</p>
          </div>
        </div>
        <button
          @click="handleLogout"
          class="mt-3 w-full text-left text-sm text-gray-400 hover:text-white transition-colors"
        >
          Sign out
        </button>
      </div>
    </aside>

    <!-- Main content -->
    <div class="flex-1 flex flex-col overflow-hidden">
      <!-- Top bar -->
      <header class="h-16 bg-white border-b border-gray-200 flex items-center px-6 shrink-0">
        <h1 class="text-lg font-semibold text-gray-900">
          {{ $route.name ? String($route.name).charAt(0).toUpperCase() + String($route.name).slice(1) : 'Dashboard' }}
        </h1>
      </header>

      <!-- Page content -->
      <main class="flex-1 overflow-y-auto p-6">
        <router-view />
      </main>
    </div>
  </div>
</template>
