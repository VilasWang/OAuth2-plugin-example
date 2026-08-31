<script setup lang="ts">
import { ref } from 'vue'
import { useRouter } from 'vue-router'
import { useAuthStore } from '../stores/auth'
import AppInput from '../components/ui/AppInput.vue'
import AppAlert from '../components/ui/AppAlert.vue'
import AppLogo from '../components/shared/AppLogo.vue'

const auth = useAuthStore()
const router = useRouter()

const username = ref('')
const password = ref('')
const loading = ref(false)

// MFA challenge state (gap-fix E2): /oauth2/login answers mfa_required with a
// short-lived mfa_token; the 6-digit TOTP code completes the login via
// auth.verifyMfa. On failure auth.loginError is set and the user stays here.
const showMfa = ref(false)
const mfaToken = ref('')
const mfaCode = ref('')
const mfaLoading = ref(false)

async function handleLogin() {
  loading.value = true
  try {
    const result = await auth.login(username.value, password.value)
    if (result?.mfaRequired) {
      mfaToken.value = result.mfaToken!
      showMfa.value = true
    } else if (!result?.error) {
      router.push('/')
    }
  } finally {
    loading.value = false
  }
}

async function handleMfa() {
  mfaLoading.value = true
  try {
    const result = await auth.verifyMfa(mfaToken.value, mfaCode.value)
    if (!result?.error) {
      router.push('/')
    }
  } finally {
    mfaLoading.value = false
  }
}

function backToLogin() {
  showMfa.value = false
  mfaCode.value = ''
  mfaToken.value = ''
  auth.loginError = ''
}
</script>

<template>
  <div class="min-h-screen flex items-center justify-center bg-page p-4">
    <div class="w-full max-w-[400px]">
      <!-- Logo -->
      <div class="flex justify-center mb-7">
        <AppLogo size="lg" />
      </div>

      <!-- Card -->
      <div class="bg-surface rounded-auth border border-neutral-200 shadow-md p-8">
        <h1 class="font-display text-[23px] leading-tight font-semibold text-neutral-900 tracking-tight">
          Sign in to Fulla Admin
        </h1>
        <p class="mt-1.5 mb-6 text-sm text-neutral-500">
          Sign in to your administrator account
        </p>
        <AppAlert
          v-if="auth.loginError"
          type="error"
          class="mb-6"
          dismissible
          @dismiss="auth.loginError = ''"
        >
          {{ auth.loginError }}
        </AppAlert>

        <form
          v-if="!showMfa"
          class="space-y-5"
          @submit.prevent="handleLogin"
        >
          <AppInput
            v-model="username"
            label="Username"
            placeholder="admin"
            required
            autocomplete="username"
          />

          <div class="space-y-1.5">
            <div class="flex items-center justify-between">
              <label
                for="password-field"
                class="block text-sm font-medium text-neutral-700"
              >
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
              class="block w-full px-3.5 py-2.5 text-sm rounded-lg border border-neutral-300 bg-surface
                     placeholder:text-neutral-400 transition-colors duration-150
                     focus:outline-none focus:ring-2 focus:ring-brand-500/20 focus:border-brand-700"
            >
          </div>

          <button
            type="submit"
            :disabled="loading"
            class="w-full inline-flex items-center justify-center gap-2 px-4 py-2.5 text-sm font-medium
                   bg-brand-600 text-white rounded-lg hover:bg-brand-700 shadow-sm
                   disabled:opacity-50 disabled:cursor-not-allowed
                   transition-all duration-150 active:scale-[0.98]
                   focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-brand-500 focus-visible:ring-offset-2"
          >
            <svg
              v-if="loading"
              class="animate-spin w-4 h-4"
              viewBox="0 0 24 24"
              fill="none"
              aria-hidden="true"
            >
              <circle
                class="opacity-25"
                cx="12"
                cy="12"
                r="10"
                stroke="currentColor"
                stroke-width="4"
              />
              <path
                class="opacity-75"
                fill="currentColor"
                d="M4 12a8 8 0 018-8V0C5.373 0 0 5.373 0 12h4z"
              />
            </svg>
            {{ loading ? 'Signing in...' : 'Sign in' }}
          </button>
        </form>

        <!-- MFA challenge: complete the login with the 6-digit TOTP code -->
        <form
          v-else
          class="space-y-5"
          @submit.prevent="handleMfa"
        >
          <div>
            <h2 class="text-base font-semibold text-neutral-900">
              Two-factor authentication
            </h2>
            <p class="mt-1 text-sm text-neutral-500">
              Enter the 6-digit code from your authenticator app.
            </p>
          </div>

          <div class="space-y-1.5">
            <label
              for="mfa-code-field"
              class="block text-sm font-medium text-neutral-700"
            >
              Authentication code
            </label>
            <input
              id="mfa-code-field"
              v-model="mfaCode"
              type="text"
              placeholder="000000"
              required
              inputmode="numeric"
              autocomplete="one-time-code"
              maxlength="6"
              class="block w-full px-3.5 py-2.5 text-sm tracking-[0.4em] text-center rounded-lg border border-neutral-300 bg-surface
                     placeholder:text-neutral-400 placeholder:tracking-normal transition-colors duration-150
                     focus:outline-none focus:ring-2 focus:ring-brand-500/20 focus:border-brand-700"
            >
          </div>

          <button
            type="submit"
            :disabled="mfaLoading || mfaCode.length !== 6"
            class="w-full inline-flex items-center justify-center gap-2 px-4 py-2.5 text-sm font-medium
                   bg-brand-600 text-white rounded-lg hover:bg-brand-700 shadow-sm
                   disabled:opacity-50 disabled:cursor-not-allowed
                   transition-all duration-150 active:scale-[0.98]
                   focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-brand-500 focus-visible:ring-offset-2"
          >
            <svg
              v-if="mfaLoading"
              class="animate-spin w-4 h-4"
              viewBox="0 0 24 24"
              fill="none"
              aria-hidden="true"
            >
              <circle
                class="opacity-25"
                cx="12"
                cy="12"
                r="10"
                stroke="currentColor"
                stroke-width="4"
              />
              <path
                class="opacity-75"
                fill="currentColor"
                d="M4 12a8 8 0 018-8V0C5.373 0 0 5.373 0 12h4z"
              />
            </svg>
            {{ mfaLoading ? 'Verifying...' : 'Verify code' }}
          </button>

          <button
            type="button"
            class="w-full text-center text-xs text-neutral-500 hover:text-neutral-700 transition-colors"
            @click="backToLogin"
          >
            ← Back to sign in
          </button>
        </form>
      </div>

      <p class="mt-6 text-center text-xs text-neutral-400">
        Fulla Identity Platform &middot; Enterprise OAuth2/OIDC Server
      </p>
    </div>
  </div>
</template>
