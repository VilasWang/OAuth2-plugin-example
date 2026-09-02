<script setup lang="ts">
import { ref } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { useI18n } from 'vue-i18n'
import axios from 'axios'
import { normalizeError } from '../../services/errorAdapter'
import AppAlert from '../../components/ui/AppAlert.vue'
import AppButton from '../../components/ui/AppButton.vue'
import AppInput from '../../components/ui/AppInput.vue'

const { t } = useI18n()
const route = useRoute()
const router = useRouter()
const token = route.query.token as string || ''
const newPassword = ref('')
const confirmPassword = ref('')
const loading = ref(false)
const error = ref('')
const success = ref(false)

async function handleReset() {
  if (newPassword.value !== confirmPassword.value) { error.value = t('common.passwordsDoNotMatch'); return }
  if (newPassword.value.length < 8) { error.value = t('common.passwordMinLength'); return }
  error.value = ''
  loading.value = true
  try {
    await axios.post('/api/password-reset/confirm', { token, new_password: newPassword.value }, { headers: { 'Content-Type': 'application/json' } })
    success.value = true
    setTimeout(() => router.push('/login'), 3000)
  } catch (e: unknown) {
    error.value = normalizeError(e).message
  } finally {
    loading.value = false
  }
}
</script>

<template>
  <div>
    <h1 class="font-display text-2xl font-bold text-neutral-900 tracking-tight text-center mb-8">
      {{ $t('auth.reset.title') }}
    </h1>

    <div
      v-if="!token"
      class="text-center text-error-600"
    >
      <p>{{ $t('auth.reset.invalidToken') }}</p>
      <router-link
        to="/forgot-password"
        class="mt-4 inline-block text-brand-600"
      >
        {{ $t('auth.reset.requestNewLink') }}
      </router-link>
    </div>

    <div
      v-else-if="success"
      class="text-center space-y-3"
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
        {{ $t('auth.reset.success') }}
      </p>
      <p class="text-sm text-neutral-500">
        {{ $t('common.redirectingToLogin') }}
      </p>
    </div>

    <form
      v-else
      class="space-y-4"
      @submit.prevent="handleReset"
    >
      <AppAlert
        v-if="error"
        type="error"
      >
        {{ error }}
      </AppAlert>
      <AppInput
        v-model="newPassword"
        :label="$t('common.newPassword')"
        type="password"
        required
        autocomplete="new-password"
        placeholder="••••••••"
      />
      <AppInput
        v-model="confirmPassword"
        :label="$t('common.confirmPassword')"
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
        {{ loading ? $t('auth.reset.resetting') : $t('auth.reset.submit') }}
      </AppButton>
    </form>
  </div>
</template>
