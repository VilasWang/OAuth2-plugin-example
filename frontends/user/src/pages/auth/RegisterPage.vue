<script setup lang="ts">
import { ref, computed } from 'vue'
import { useRouter } from 'vue-router'
import axios from 'axios'
import { normalizeError } from '../../services/errorAdapter'
import AppAlert from '../../components/ui/AppAlert.vue'
import AppButton from '../../components/ui/AppButton.vue'
import AppInput from '../../components/ui/AppInput.vue'
import { passwordStrength } from '../../utils/passwordStrength'

const router = useRouter()
const username = ref('')
const email = ref('')
const password = ref('')
const confirmPassword = ref('')
const error = ref('')
const loading = ref(false)
const success = ref(false)

// Password strength meter (mockup 17): 4 segments, error -> warning ->
// success progression. Scoring lives in utils/passwordStrength.ts (unit
// tested; the scale reaches 4 so every segment can light).
const strength = computed(() => passwordStrength(password.value))

async function handleRegister() {
  error.value = ''
  if (password.value !== confirmPassword.value) {
    error.value = 'Passwords do not match'
    return
  }
  if (password.value.length < 8) {
    error.value = 'Password must be at least 8 characters'
    return
  }
  loading.value = true
  try {
    await axios.post('/api/register', new URLSearchParams({
      username: username.value,
      password: password.value,
      email: email.value,
    }))
    success.value = true
    setTimeout(() => router.push('/login'), 2000)
  } catch (e: unknown) {
    error.value = normalizeError(e).message
  } finally {
    loading.value = false
  }
}
</script>

<template>
  <div>
    <div class="mb-8">
      <h1 class="font-display text-2xl font-bold text-neutral-900 tracking-tight">
        Create Account
      </h1>
      <p class="mt-2 text-sm text-neutral-500">
        Join us today
      </p>
    </div>

    <div
      v-if="success"
      class="text-center space-y-4 py-4"
    >
      <div class="w-16 h-16 bg-success-100 rounded-card flex items-center justify-center mx-auto">
        <svg
          class="w-8 h-8 text-success-600"
          viewBox="0 0 20 20"
          fill="currentColor"
          aria-hidden="true"
        >
          <path fill-rule="evenodd" d="M16.704 4.153a.75.75 0 01.143 1.052l-8 10.5a.75.75 0 01-1.127.075l-4.5-4.5a.75.75 0 011.06-1.06l3.894 3.893 7.48-9.817a.75.75 0 011.05-.143z" clip-rule="evenodd" />
        </svg>
      </div>
      <p class="text-neutral-700 font-medium">
        Account created successfully!
      </p>
      <p class="text-sm text-neutral-500">
        Redirecting to login...
      </p>
    </div>

    <AppAlert
      v-if="error"
      type="error"
      class="mb-4"
    >
      {{ error }}
    </AppAlert>

    <form
      v-if="!success"
      class="space-y-4"
      @submit.prevent="handleRegister"
    >
      <AppInput
        v-model="email"
        label="Email"
        type="email"
        required
        autocomplete="email"
        placeholder="you@example.com"
      />
      <AppInput
        v-model="username"
        label="Username"
        hint="Optional — generated for you when left blank"
        autocomplete="username"
        placeholder="mia"
      />
      <div>
        <AppInput
          v-model="password"
          label="Password"
          type="password"
          required
          autocomplete="new-password"
          placeholder="••••••••"
        />
        <!-- Strength meter: 4 hairline segments (mockup .pw-meter) -->
        <div
          v-if="password"
          class="flex gap-1.5 mt-2"
          aria-hidden="true"
        >
          <span
            v-for="i in 4"
            :key="i"
            class="h-[3px] flex-1 rounded-full transition-colors duration-150"
            :class="i <= strength
              ? (strength <= 1 ? 'bg-error-500' : strength <= 2 ? 'bg-warning-500' : 'bg-success-500')
              : 'bg-neutral-200'"
          />
        </div>
        <p class="text-xs text-neutral-500 mt-1.5">
          Minimum 8 characters. A longer passphrase of 3–4 random words works well.
        </p>
      </div>
      <AppInput
        v-model="confirmPassword"
        label="Confirm Password"
        type="password"
        required
        autocomplete="new-password"
        placeholder="••••••••"
      />
      <AppButton
        type="submit"
        :loading="loading"
        block
      >
        {{ loading ? 'Creating...' : 'Create Account' }}
      </AppButton>
    </form>

    <p class="mt-6 text-center text-sm text-neutral-500">
      Already have an account?
      <router-link
        to="/login"
        class="text-brand-600 font-medium hover:text-brand-800"
      >
        Sign in
      </router-link>
    </p>
  </div>
</template>
