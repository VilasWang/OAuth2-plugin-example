<script setup lang="ts">
import { ref } from 'vue'
import { useRouter, useRoute } from 'vue-router'
import { useAuthStore } from '../../stores/auth'
import AppInput from '../../components/ui/AppInput.vue'
import AppButton from '../../components/ui/AppButton.vue'
import AppAlert from '../../components/ui/AppAlert.vue'

const auth = useAuthStore()
const router = useRouter()
const route = useRoute()

const username = ref('')
const password = ref('')
const mfaCode = ref('')
const mfaToken = ref('')
const showMfa = ref(false)

const GITHUB_CLIENT_ID = import.meta.env.VITE_GITHUB_CLIENT_ID || ''
const githubAuthUrl = `https://github.com/login/oauth/authorize?client_id=${GITHUB_CLIENT_ID}&scope=user:email&redirect_uri=${encodeURIComponent(window.location.origin + '/callback/github')}`

async function handleLogin() {
  const result = await auth.login(username.value, password.value)
  if (result.mfaRequired) {
    mfaToken.value = result.mfaToken!
    showMfa.value = true
  } else if (result.success) {
    router.push((route.query.redirect as string) || '/')
  }
}

async function handleMfa() {
  const result = await auth.verifyMfa(mfaToken.value, mfaCode.value)
  if (result.success) {
    router.push((route.query.redirect as string) || '/')
  }
}
</script>

<template>
  <div>
    <!-- Header -->
    <div class="mb-8">
      <h1 class="text-2xl font-bold text-neutral-900 tracking-tight">
        Sign in to your account
      </h1>
      <p class="mt-2 text-sm text-neutral-500">
        Or
        <router-link
          to="/register"
          class="text-brand-700 font-medium hover:text-brand-700 transition-colors"
        >
          create a new account
        </router-link>
      </p>
    </div>

    <AppAlert
      v-if="auth.error"
      type="error"
      class="mb-6"
      dismissible
    >
      {{ auth.error }}
    </AppAlert>

    <!-- MFA Challenge -->
    <form
      v-if="showMfa"
      class="space-y-6"
      @submit.prevent="handleMfa"
    >
      <div class="text-center py-4">
        <div class="w-14 h-14 rounded-2xl bg-brand-50 flex items-center justify-center mx-auto mb-4">
          <svg
            class="w-7 h-7 text-brand-700"
            viewBox="0 0 20 20"
            fill="currentColor"
          >
            <path
              fill-rule="evenodd"
              d="M10 2a4 4 0 00-4 4v4H4a2 2 0 00-2 2v4a2 2 0 002 2h12a2 2 0 002-2v-4a2 2 0 00-2-2h-2V6a4 4 0 00-4-4zm1.5 6.5a1 1 0 11-2 0 1 1 0 012 0zM4 14h12v4H4v-4z"
            />
          </svg>
        </div>
        <h2 class="text-lg font-semibold text-neutral-900">
          Two-Factor Authentication
        </h2>
        <p class="text-sm text-neutral-500 mt-1">
          Enter the 6-digit code from your authenticator app
        </p>
      </div>
      <div>
        <input
          v-model="mfaCode"
          type="text"
          inputmode="numeric"
          maxlength="6"
          autocomplete="one-time-code"
          class="block w-full px-4 py-4 text-center text-2xl tracking-[0.42em] font-mono tabular-nums border border-neutral-300 rounded-ctl bg-surface
                 focus:outline-none focus-visible:ring-[3px] focus-visible:ring-ring focus:border-brand-700"
          placeholder="000000"
        >
      </div>
      <AppButton
        type="submit"
        :loading="auth.loading"
        :disabled="mfaCode.length !== 6"
        block
      >
        Verify Code
      </AppButton>
      <button
        type="button"
        class="w-full text-sm text-neutral-500 hover:text-neutral-700 transition-colors"
        @click="showMfa = false; mfaCode = ''"
      >
        Back to sign in
      </button>
    </form>

    <!-- Login Form -->
    <form
      v-else
      class="space-y-5"
      @submit.prevent="handleLogin"
    >
      <AppInput
        v-model="username"
        label="Email or Username"
        placeholder="you@example.com"
        required
        autocomplete="username"
      />

      <AppInput
        v-model="password"
        label="Password"
        type="password"
        placeholder="Enter your password"
        required
        autocomplete="current-password"
      />

      <div class="flex justify-end">
        <router-link
          to="/forgot-password"
          class="text-sm text-brand-700 hover:text-brand-800 font-medium transition-colors"
        >
          Forgot password?
        </router-link>
      </div>

      <AppButton
        type="submit"
        :loading="auth.loading"
        block
      >
        Sign In
      </AppButton>

      <!-- Social Login Divider -->
      <div
        v-if="GITHUB_CLIENT_ID"
        class="relative my-6"
      >
        <div class="absolute inset-0 flex items-center">
          <div class="w-full border-t border-neutral-200" />
        </div>
        <div class="relative flex justify-center text-sm">
          <span class="px-3 bg-surface text-neutral-400">or continue with</span>
        </div>
      </div>

      <!-- GitHub Login -->
      <a
        v-if="GITHUB_CLIENT_ID"
        :href="githubAuthUrl"
        class="w-full flex items-center justify-center gap-3 px-4 py-2.5 border border-neutral-300
               rounded-lg text-sm font-medium text-neutral-700 hover:bg-neutral-50 transition-colors"
      >
        <svg
          class="w-5 h-5"
          viewBox="0 0 24 24"
          fill="currentColor"
          aria-hidden="true"
        >
          <path d="M12 0C5.37 0 0 5.37 0 12c0 5.31 3.435 9.795 8.205 11.385.6.105.825-.255.825-.57 0-.285-.015-1.23-.015-2.235-3.015.555-3.795-.735-4.035-1.41-.135-.345-.72-1.41-1.23-1.695-.42-.225-1.02-.78-.015-.795.945-.015 1.62.87 1.845 1.23 1.08 1.815 2.805 1.305 3.495.99.105-.78.42-1.305.765-1.605-2.67-.3-5.46-1.335-5.46-5.925 0-1.305.465-2.385 1.23-3.225-.12-.3-.54-1.53.12-3.18 0 0 1.005-.315 3.3 1.23.96-.27 1.98-.405 3-.405s2.04.135 3 .405c2.295-1.56 3.3-1.23 3.3-1.23.66 1.65.24 2.88.12 3.18.765.84 1.23 1.905 1.23 3.225 0 4.605-2.805 5.625-5.475 5.925.435.375.81 1.095.81 2.22 0 1.605-.015 2.895-.015 3.3 0 .315.225.69.825.57A12.02 12.02 0 0024 12c0-6.63-5.37-12-12-12z" />
        </svg>
        Sign in with GitHub
      </a>
    </form>
  </div>
</template>
