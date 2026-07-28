<script setup lang="ts">
import { ref } from 'vue'
import { useRouter } from 'vue-router'
import { useAuthStore } from '../stores/auth'
import AppLogo from '../components/shared/AppLogo.vue'

const auth = useAuthStore()
const router = useRouter()
const showUserMenu = ref(false)

async function handleLogout() {
  await auth.logout()
  router.push('/login')
}

const navItems = [
  { name: 'Overview', path: '/', icon: '🏠' },
  { name: 'Profile', path: '/profile', icon: '👤' },
  { name: 'Security', path: '/security', icon: '🔒' },
  { name: 'Authorized Apps', path: '/authorized-apps', icon: '📱' },
]
</script>

<template>
  <div class="min-h-screen bg-gray-50">
    <!-- Top Navigation -->
    <header class="bg-white border-b border-gray-200 sticky top-0 z-40">
      <div class="max-w-6xl mx-auto px-4 sm:px-6 lg:px-8">
        <div class="flex justify-between h-16">
          <!-- Left: Logo + Nav -->
          <div class="flex items-center gap-8">
            <router-link to="/" class="shrink-0">
              <AppLogo />
            </router-link>
            <nav class="hidden md:flex items-center gap-1">
              <router-link v-for="item in navItems" :key="item.path" :to="item.path"
                class="px-3 py-2 rounded-md text-sm font-medium transition-colors"
                :class="$route.path === item.path
                  ? 'bg-indigo-50 text-indigo-700'
                  : 'text-gray-600 hover:text-gray-900 hover:bg-gray-50'">
                {{ item.name }}
              </router-link>
            </nav>
          </div>

          <!-- Right: User Menu -->
          <div class="flex items-center">
            <div class="relative">
              <button @click="showUserMenu = !showUserMenu"
                class="flex items-center gap-2 px-3 py-1.5 rounded-lg hover:bg-gray-100 transition-colors">
                <div class="w-8 h-8 rounded-full bg-indigo-100 text-indigo-700 flex items-center justify-center text-sm font-semibold">
                  {{ (auth.user?.name || 'U')[0].toUpperCase() }}
                </div>
                <span class="hidden sm:block text-sm font-medium text-gray-700">{{ auth.user?.name || 'User' }}</span>
                <svg class="w-4 h-4 text-gray-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                  <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M19 9l-7 7-7-7" />
                </svg>
              </button>

              <!-- Dropdown -->
              <div v-if="showUserMenu" @click="showUserMenu = false"
                class="absolute right-0 mt-2 w-56 bg-white rounded-xl shadow-lg border border-gray-200 py-2 z-50">
                <div class="px-4 py-2 border-b border-gray-100">
                  <p class="text-sm font-medium text-gray-900">{{ auth.user?.name }}</p>
                  <p class="text-xs text-gray-500 truncate">{{ auth.user?.email }}</p>
                </div>
                <router-link to="/profile" class="block px-4 py-2 text-sm text-gray-700 hover:bg-gray-50">Profile</router-link>
                <router-link to="/security" class="block px-4 py-2 text-sm text-gray-700 hover:bg-gray-50">Security</router-link>
                <div class="border-t border-gray-100 mt-1 pt-1">
                  <button @click="handleLogout" class="w-full text-left px-4 py-2 text-sm text-rose-600 hover:bg-rose-50">Sign Out</button>
                </div>
              </div>
            </div>
          </div>
        </div>
      </div>
    </header>

    <!-- Page Content -->
    <main class="max-w-6xl mx-auto px-4 sm:px-6 lg:px-8 py-8">
      <router-view />
    </main>
  </div>

  <!-- Click outside to close menu -->
  <div v-if="showUserMenu" @click="showUserMenu = false" class="fixed inset-0 z-30"></div>
</template>
