<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { useI18n } from 'vue-i18n'
import http from '../../services/http'
import { normalizeError } from '../../services/errorAdapter'
import AppAlert from '../../components/ui/AppAlert.vue'
import AppCard from '../../components/ui/AppCard.vue'
import AppEmptyState from '../../components/ui/AppEmptyState.vue'
import DData from '../../components/ui/DData.vue'

const { t } = useI18n()
const apps = ref<any[]>([])
const loading = ref(true)
const error = ref('')
const success = ref('')

async function fetchApps() {
  loading.value = true
  try {
    const resp = await http.get('/api/me/authorized-apps')
    // Backend envelope: {authorized_apps: [...], total} (UserSelfServiceController).
    // The old `resp.data.apps ||` first fallback matched only a mock shape the
    // real backend never returns — PR-review cleanup removed it so the page
    // parses the actual contract.
    apps.value = resp.data?.authorized_apps || []
  } catch {
    error.value = t('account.authorizedApps.loadFailed')
  } finally {
    loading.value = false
  }
}

async function revokeApp(clientId: string, appName: string) {
  if (!confirm(t('account.authorizedApps.revokeConfirm', { app: appName }))) return
  try {
    await http.delete(`/api/me/authorized-apps/${clientId}`)
    success.value = t('account.authorizedApps.revoked', { app: appName })
    setTimeout(() => { success.value = '' }, 3000)
    await fetchApps()
  } catch (e: unknown) {
    error.value = normalizeError(e).message
  }
}

onMounted(fetchApps)
</script>

<template>
  <div>
    <h1 class="text-2xl font-bold text-neutral-900 mb-6">
      {{ $t('account.authorizedApps.title') }}
    </h1>
    <p class="text-neutral-500 mb-6">
      {{ $t('account.authorizedApps.intro') }}
    </p>

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
      v-else-if="apps.length === 0"
      padding="none"
    >
      <AppEmptyState
        :title="$t('account.authorizedApps.emptyTitle')"
        :description="$t('account.authorizedApps.emptyDesc')"
      />
    </AppCard>

    <div
      v-else
      class="space-y-3"
    >
      <AppCard
        v-for="app in apps"
        :key="app.client_id"
        padding="sm"
      >
        <div class="flex items-center justify-between">
        <div>
          <p class="font-medium text-neutral-900">
            {{ app.name || app.client_id }}
          </p>
          <div class="flex items-center gap-1.5 mt-0.5">
            <span class="text-sm text-neutral-500">{{ $t('account.authorizedApps.clientId') }}</span>
            <DData :value="app.client_id" />
          </div>
          <div
            v-if="app.scope"
            class="flex flex-wrap items-center gap-1.5 mt-1.5"
          >
            <span class="text-xs text-neutral-400">{{ $t('account.authorizedApps.scopes') }}</span>
            <DData
              v-for="s in app.scope.split(' ').filter(Boolean)"
              :key="s"
              :value="s"
            />
          </div>
        </div>
        <button
          class="px-3 py-1.5 text-sm text-error-600 border border-error-200 rounded-ctl hover:bg-error-50 transition-colors
                 focus-visible:outline-none focus-visible:ring-[3px] focus-visible:ring-ring"
          @click="revokeApp(app.client_id, app.name || app.client_id)"
        >
          {{ $t('account.authorizedApps.revoke') }}
        </button>
        </div>
      </AppCard>
    </div>
  </div>
</template>

