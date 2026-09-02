<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { useRoute } from 'vue-router'
import { useI18n } from 'vue-i18n'
import axios from 'axios'
import { normalizeError } from '../../services/errorAdapter'

const { t } = useI18n()
const route = useRoute()
const status = ref<'loading' | 'success' | 'error'>('loading')
const message = ref('')

onMounted(async () => {
  const token = route.query.token as string
  if (!token) {
    status.value = 'error'
    message.value = t('auth.verify.missingToken')
    return
  }
  try {
    const resp = await axios.get(`/api/verify-email?token=${encodeURIComponent(token)}`)
    status.value = 'success'
    message.value = resp.data?.message || t('auth.verify.successDefault')
  } catch (e: unknown) {
    status.value = 'error'
    message.value = normalizeError(e).message
  }
})
</script>

<template>
  <div class="text-center">
    <div v-if="status === 'loading'">
      <div class="animate-spin w-8 h-8 border-4 border-brand-600 border-t-transparent rounded-full mx-auto" />
      <p class="mt-4 text-neutral-600">
        {{ $t('auth.verify.verifying') }}
      </p>
    </div>
    <div v-else-if="status === 'success'">
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
      <h2 class="mt-4 text-xl font-bold text-neutral-900">
        {{ $t('auth.verify.successTitle') }}
      </h2>
      <p class="mt-2 text-neutral-600">
        {{ message }}
      </p>
      <router-link
        to="/login"
        class="mt-6 inline-block px-6 py-2.5 bg-brand-600 text-white text-sm font-medium rounded-ctl hover:bg-brand-700
               focus-visible:outline-none focus-visible:ring-[3px] focus-visible:ring-ring transition-colors shadow-sm"
      >
        {{ $t('common.goToLogin') }}
      </router-link>
    </div>
    <div v-else>
      <div class="w-16 h-16 bg-error-100 rounded-card flex items-center justify-center mx-auto">
        <svg
          class="w-8 h-8 text-error-600"
          viewBox="0 0 20 20"
          fill="currentColor"
          aria-hidden="true"
        >
          <path fill-rule="evenodd" d="M10 18a8 8 0 100-16 8 8 0 000 16zM8.28 7.22a.75.75 0 00-1.06 1.06L8.94 10l-1.72 1.72a.75.75 0 101.06 1.06L10 11.06l1.72 1.72a.75.75 0 101.06-1.06L11.06 10l1.72-1.72a.75.75 0 00-1.06-1.06L10 8.94 8.28 7.22z" clip-rule="evenodd" />
        </svg>
      </div>
      <h2 class="mt-4 text-xl font-bold text-neutral-900">
        {{ $t('auth.verify.errorTitle') }}
      </h2>
      <p class="mt-2 text-neutral-600">
        {{ message }}
      </p>
      <router-link
        to="/login"
        class="mt-6 inline-block px-6 py-2.5 bg-brand-600 text-white text-sm font-medium rounded-ctl hover:bg-brand-700
               focus-visible:outline-none focus-visible:ring-[3px] focus-visible:ring-ring transition-colors shadow-sm"
      >
        {{ $t('common.goToLogin') }}
      </router-link>
    </div>
  </div>
</template>
