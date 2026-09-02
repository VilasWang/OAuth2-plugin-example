<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { useRouter, useRoute } from 'vue-router'
import { useI18n } from 'vue-i18n'
import { useAuthStore } from '../../stores/auth'
import { normalizeError } from '../../services/errorAdapter'
import { getErrorMessage } from '../../services/messages'

const { t } = useI18n()
const router = useRouter()
const route = useRoute()
const auth = useAuthStore()
const error = ref('')
// Secondary detail: the raw error_description from the redirect, kept only
// when it exists and adds something the catalog-resolved message lacks.
const errorDetail = ref('')

onMounted(async () => {
  const code = route.query.code as string
  const errorParam = route.query.error as string

  if (typeof errorParam === 'string' && errorParam) {
    // Resolve the protocol error code through the shared message catalog
    // (covers every OAuth2/OIDC code; unknown codes fall back to the generic
    // message) instead of rendering error_description raw.
    error.value = getErrorMessage(errorParam)
    const description = route.query.error_description
    errorDetail.value =
      typeof description === 'string' && description && description !== error.value
        ? description
        : ''
    return
  }

  if (!code) {
    error.value = t('oauth.callback.noCode')
    return
  }

  try {
    await auth.exchangeCode(code)
    router.replace('/')
  } catch (e: unknown) {
    error.value = normalizeError(e).message
  }
})
</script>

<template>
  <div class="min-h-screen flex items-center justify-center">
    <div class="text-center">
      <div
        v-if="error"
        class="p-6 bg-error-50 border border-error-200 rounded-lg max-w-md"
      >
        <p class="text-error-700 font-medium">
          {{ $t('oauth.callback.errorTitle') }}
        </p>
        <p class="text-error-600 text-sm mt-2">
          {{ error }}
        </p>
        <p
          v-if="errorDetail"
          class="text-error-500 text-xs mt-1.5 break-words"
        >
          {{ errorDetail }}
        </p>
        <router-link
          to="/login"
          class="mt-4 inline-block text-brand-600 hover:text-brand-800"
        >
          {{ $t('common.backToLogin') }}
        </router-link>
      </div>
      <div v-else>
        <div class="animate-spin w-8 h-8 border-4 border-brand-600 border-t-transparent rounded-full mx-auto" />
        <p class="mt-4 text-neutral-600">
          {{ $t('oauth.callback.completing') }}
        </p>
      </div>
    </div>
  </div>
</template>
