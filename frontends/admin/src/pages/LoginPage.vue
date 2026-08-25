<script setup lang="ts">
import { ref } from 'vue'
import { useRouter, useRoute } from 'vue-router'
import { useAuthStore } from '../stores/auth'
import AppInput from '../components/ui/AppInput.vue'
import AppAlert from '../components/ui/AppAlert.vue'

const auth = useAuthStore()
const router = useRouter()
const route = useRoute()

const username = ref('')
const password = ref('')
const loading = ref(false)

async function handleLogin() {
  loading.value = true
  try {
    const result = await auth.login(username.value, password.value)
    if (!result?.error) {
      router.push('/')
    }
  } finally {
    loading.value = false
  }
}
</script>

<template>
  <div class="min-h-screen flex items-center justify-center bg-neutral-50 p-4">
    <div class="w-full max-w-[400px]">
      <!-- Logo -->
      <div class="text-center mb-8">
        <div class="inline-flex items-center justify-center w-12 h-12 rounded-xl bg-sky-700 text-white font-bold text-lg mb-4">
          AF
        </div>
        <h1 class="text-xl font-bold text-neutral-900 tracking-tight">Fulla Admin</h1>
        <p class="mt-1.5 text-sm text-neutral-500">Sign in to your administrator account</p>
      </div>

      <!-- Card -->
      <div class="bg-white rounded-xl border border-neutral-200 shadow-sm p-8">
        <AppAlert v-if="auth.loginError" type="error" class="mb-6" dismissible @dismiss="auth.loginError = ''">
          {{ auth.loginError }}
        </AppAlert>

        <form @submit.prevent="handleLogin" class="space-y-5">
          <AppInput
            v-model="username"
            label="Username"
            placeholder="admin"
            required
            autocomplete="username"
          />

          <div class="space-y-1.5">
            <div class="flex items-center justify-between">
              <label for="password-field" class="block text-sm font-medium text-neutral-700">
                Password
              </label>
            </div>
            <input
              id="password-field"
              v-model="password"
              type="password"
              placeholder="Enter your password"
              required
              autocomplete="current-password"
              class="block w-full px-3.5 py-2.5 text-sm rounded-lg border border-neutral-300 bg-white
                     placeholder:text-neutral-400 transition-colors duration-150
                     focus:outline-none focus:ring-2 focus:ring-sky-500/20 focus:border-sky-600"
            />
          </div>

          <button
            type="submit"
            :disabled="loading"
            class="w-full inline-flex items-center justify-center gap-2 px-4 py-2.5 text-sm font-medium
                   bg-sky-700 text-white rounded-lg hover:bg-sky-800 shadow-sm
                   disabled:opacity-50 disabled:cursor-not-allowed
                   transition-all duration-150 active:scale-[0.98]
                   focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-sky-500 focus-visible:ring-offset-2"
          >
            <svg v-if="loading" class="animate-spin w-4 h-4" viewBox="0 0 24 24" fill="none" aria-hidden="true">
              <circle class="opacity-25" cx="12" cy="12" r="10" stroke="currentColor" stroke-width="4" />
              <path class="opacity-75" fill="currentColor" d="M4 12a8 8 0 018-8V0C5.373 0 0 5.373 0 12h4z" />
            </svg>
            {{ loading ? 'Signing in...' : 'Sign in' }}
          </button>
        </form>
      </div>

      <p class="mt-6 text-center text-xs text-neutral-400">
        Fulla Identity Platform &middot; Enterprise OAuth2/OIDC Server
      </p>
    </div>
  </div>
</template>
