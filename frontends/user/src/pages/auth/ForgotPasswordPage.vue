<script setup lang="ts">
import { ref } from 'vue'
import axios from 'axios'
import AppButton from '../../components/ui/AppButton.vue'
import AppInput from '../../components/ui/AppInput.vue'

const email = ref('')
const loading = ref(false)
const sent = ref(false)
const error = ref('')

async function handleSubmit() {
  error.value = ''
  loading.value = true
  try {
    await axios.post('/api/password-reset/request', { email: email.value }, { headers: { 'Content-Type': 'application/json' } })
    sent.value = true
  } catch {
    // Always show success (anti-enumeration)
    sent.value = true
  } finally {
    loading.value = false
  }
}
</script>

<template>
  <div>
    <div class="mb-8">
      <h1 class="font-display text-2xl font-bold text-neutral-900 tracking-tight">
        Reset Password
      </h1>
      <p class="mt-2 text-sm text-neutral-500">
        Enter your email to receive a reset link
      </p>
    </div>

    <div
      v-if="sent"
      class="text-center space-y-4"
    >
      <div class="w-16 h-16 bg-success-100 rounded-card flex items-center justify-center mx-auto">
        <svg
          class="w-8 h-8 text-success-600"
          viewBox="0 0 24 24"
          fill="none"
          stroke="currentColor"
          stroke-width="1.5"
          aria-hidden="true"
        >
          <path stroke-linecap="round" stroke-linejoin="round" d="M21.75 6.75v10.5a2.25 2.25 0 01-2.25 2.25h-15a2.25 2.25 0 01-2.25-2.25V6.75m19.5 0A2.25 2.25 0 0019.5 4.5h-15a2.25 2.25 0 00-2.25 2.25m19.5 0v.243a2.25 2.25 0 01-1.07 1.916l-7.5 4.615a2.25 2.25 0 01-2.36 0L3.32 8.91a2.25 2.25 0 01-1.07-1.916V6.75" />
        </svg>
      </div>
      <p class="text-neutral-700">
        If an account with that email exists, we've sent a password reset link.
      </p>
      <p class="text-sm text-neutral-500">
        Check your inbox and spam folder.
      </p>
      <router-link
        to="/login"
        class="inline-block mt-2 text-brand-600 hover:text-brand-800 font-medium"
      >
        Back to Login
      </router-link>
    </div>

    <form
      v-else
      class="space-y-5"
      @submit.prevent="handleSubmit"
    >
      <AppInput
        v-model="email"
        label="Email Address"
        type="email"
        required
        autocomplete="email"
        placeholder="you@example.com"
      />
      <AppButton
        type="submit"
        :loading="loading"
        block
      >
        {{ loading ? 'Sending...' : 'Send Reset Link' }}
      </AppButton>
      <p class="text-center text-sm text-neutral-500">
        <router-link
          to="/login"
          class="text-brand-600 hover:text-brand-800"
        >
          Back to Login
        </router-link>
      </p>
    </form>
  </div>
</template>
