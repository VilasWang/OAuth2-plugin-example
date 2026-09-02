<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { useI18n } from 'vue-i18n'
import { useAuthStore } from '../../stores/auth'
import http from '../../services/http'
import { normalizeError } from '../../services/errorAdapter'
import AppAlert from '../../components/ui/AppAlert.vue'
import AppBadge from '../../components/ui/AppBadge.vue'
import AppCard from '../../components/ui/AppCard.vue'
import DData from '../../components/ui/DData.vue'

const { t } = useI18n()
const auth = useAuthStore()
const profile = ref<any>(null)
const loading = ref(true)
const success = ref('')
const error = ref('')

async function fetchProfile() {
  loading.value = true
  try {
    const resp = await http.get('/api/me')
    profile.value = resp.data
  } catch {
    error.value = t('account.profile.loadFailed')
  } finally {
    loading.value = false
  }
}

async function resendVerification() {
  try {
    await http.post('/api/verify-email/resend')
    success.value = t('account.profile.verificationSent')
    setTimeout(() => { success.value = '' }, 3000)
  } catch (e: unknown) {
    error.value = normalizeError(e).message
  }
}

onMounted(fetchProfile)
</script>

<template>
  <div>
    <h1 class="text-2xl font-bold text-neutral-900 mb-6">
      {{ $t('nav.profile') }}
    </h1>

    <AppAlert
      v-if="success"
      type="success"
      class="mb-4"
    >
      {{ success }}
    </AppAlert>
    <AppAlert
      v-if="error"
      type="error"
      class="mb-4"
    >
      {{ error }}
    </AppAlert>

    <div
      v-if="loading"
      class="text-center py-12 text-neutral-500"
    >
      {{ $t('common.loading') }}
    </div>

    <AppCard
      v-else
      class="space-y-6"
    >
      <div class="grid grid-cols-1 md:grid-cols-2 gap-6">
        <div>
          <label class="block text-sm font-medium text-neutral-500">{{ $t('common.username') }}</label>
          <p class="mt-1 text-lg font-medium text-neutral-900">
            {{ profile?.username || auth.user?.name }}
          </p>
        </div>
        <div>
          <label class="block text-sm font-medium text-neutral-500">{{ $t('account.profile.accountIdSub') }}</label>
          <div class="mt-1">
            <DData
              :value="auth.user?.sub || '—'"
              truncate
            />
          </div>
        </div>
        <div>
          <label class="block text-sm font-medium text-neutral-500">{{ $t('common.email') }}</label>
          <div class="flex items-center gap-2 mt-1">
            <p class="text-neutral-900">
              {{ profile?.email || 'N/A' }}
            </p>
            <AppBadge
              v-if="profile?.email_verified"
              variant="success"
              size="sm"
            >{{ $t('account.profile.verified') }}</AppBadge>
            <AppBadge
              v-else
              variant="warning"
              size="sm"
            >{{ $t('account.profile.unverified') }}</AppBadge>
          </div>
          <button
            v-if="profile?.email && !profile?.email_verified"
            class="mt-2 text-sm text-brand-600 hover:text-brand-800"
            @click="resendVerification"
          >
            {{ $t('account.profile.resendVerification') }}
          </button>
        </div>
        <div>
          <label class="block text-sm font-medium text-neutral-500">{{ $t('common.roles') }}</label>
          <div class="flex flex-wrap gap-1.5 mt-1">
            <AppBadge
              v-for="role in (auth.user?.roles || [])"
              :key="role"
              variant="info"
              size="sm"
            >{{ role }}</AppBadge>
          </div>
        </div>
      </div>
    </AppCard>
  </div>
</template>

