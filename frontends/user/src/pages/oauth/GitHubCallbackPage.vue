<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { useRouter, useRoute } from 'vue-router'
import { useI18n } from 'vue-i18n'
import { useAuthStore } from '../../stores/auth'
import { setTokens, getAccessToken, tryRestoreSession } from '../../services/http'
import { userService } from '../../services/userService'
import { normalizeError } from '../../services/errorAdapter'
import axios from 'axios'

const { t } = useI18n()
const router = useRouter()
const route = useRoute()
const auth = useAuthStore()
const error = ref('')

onMounted(async () => {
  const code = route.query.code as string
  if (!code) {
    error.value = t('oauth.noCodeFromProvider', { provider: 'GitHub' })
    return
  }

  // Link flow (#71): SecurityPage's beginSocialLink sets a sessionStorage
  // marker before redirecting, and the link callback always carries the
  // server-minted non-empty state (login sends none -- LoginPage builds no
  // state). Either signal identifies the link flow. MUST short-circuit
  // before the login POST below, and must NEVER fall through to it: the
  // OAuth round-trip is a full page reload, so the in-memory access token is
  // gone even for a signed-in user -- restore the session first; only if
  // that fails (logged out / expired) show an error. A fall-through would
  // silently mint a login session, and for an unmapped identity auto-CREATE
  // an account (review W1).
  const queryState = typeof route.query.state === 'string' ? route.query.state : ''
  const isLinkFlow =
    sessionStorage.getItem('social_link_flow') === 'github' || queryState.length > 0
  if (isLinkFlow) {
    sessionStorage.removeItem('social_link_flow')
    if (!queryState) {
      error.value = t('oauth.linkMissingState')
      return
    }
    const hasSession = getAccessToken() ? true : await tryRestoreSession()
    if (!hasSession) {
      error.value = t('oauth.linkSignInFirst', { provider: 'GitHub' })
      return
    }
    try {
      await userService.linkSocialAccount('github', code, queryState)
      router.replace('/security')
      return
    } catch (e: unknown) {
      error.value = normalizeError(e).message
      return
    }
  }

  try {
    const resp = await axios.post('/api/github/login', { code }, {
      headers: { 'Content-Type': 'application/json' },
    })

    if (resp.data.access_token) {
      // Backend returned tokens — complete login
      setTokens(resp.data.access_token, resp.data.refresh_token)
      auth.markAuthenticated()
      await auth.fetchUser()
      router.replace('/')
    } else {
      error.value = t('oauth.noAccessToken', { provider: 'GitHub' })
    }
  } catch (e: unknown) {
    error.value = normalizeError(e).message
  }
})
</script>

<template>
  <div class="min-h-screen flex items-center justify-center bg-page blueprint-grid px-4">
    <div class="fixed inset-x-0 top-0 h-[3px] bg-brand-600 z-10" />
    <div class="text-center max-w-md w-full">
      <div
        v-if="error"
        class="p-6 bg-surface border border-error-200 rounded-card shadow-sm text-left"
      >
        <p class="text-error-700 font-medium">
          {{ $t('oauth.github.errorTitle') }}
        </p>
        <p class="text-error-600 text-sm mt-2">
          {{ error }}
        </p>
        <router-link
          to="/login"
          class="mt-4 inline-block text-brand-600 hover:text-brand-800 rounded-ctl
                 focus-visible:outline-none focus-visible:ring-[3px] focus-visible:ring-ring"
        >
          {{ $t('common.backToLogin') }}
        </router-link>
      </div>
      <div v-else>
        <div class="animate-spin w-8 h-8 border-4 border-brand-600 border-t-transparent rounded-full mx-auto" />
        <p class="mt-4 text-neutral-600">
          {{ $t('oauth.github.completing') }}
        </p>
      </div>
    </div>
  </div>
</template>
