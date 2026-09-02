<script setup lang="ts">
// #70: generalized social-login callback. Same contract as
// GitHubCallbackPage (which stays the /callback/github route for e2e
// stability), parameterized by the route's meta.provider:
//   1. link flow (#71): a sessionStorage `social_link_flow` marker (set by
//      SecurityPage's beginSocialLink) or a non-empty `state` query identifies
//      it; short-circuits to userService.linkSocialAccount — NEVER falls
//      through to the login POST (that would silently mint a login session
//      and possibly auto-CREATE an account, review W1).
//   2. login flow: POST /api/{provider}/login { code } -> first-party token
//      pair (#70) -> setTokens + fetchUser + redirect home.
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

const provider = (route.meta.provider as string) || 'google'
const providerLabel = provider.charAt(0).toUpperCase() + provider.slice(1)

onMounted(async () => {
  const code = route.query.code as string
  if (!code) {
    error.value = t('oauth.noCodeFromProvider', { provider: providerLabel })
    return
  }

  const queryState = typeof route.query.state === 'string' ? route.query.state : ''
  const isLinkFlow =
    sessionStorage.getItem('social_link_flow') === provider || queryState.length > 0
  if (isLinkFlow) {
    sessionStorage.removeItem('social_link_flow')
    if (!queryState) {
      error.value = t('oauth.linkMissingState')
      return
    }
    const hasSession = getAccessToken() ? true : await tryRestoreSession()
    if (!hasSession) {
      error.value = t('oauth.linkSignInFirst', { provider: providerLabel })
      return
    }
    try {
      await userService.linkSocialAccount(provider, code, queryState)
      router.replace('/security')
      return
    } catch (e: unknown) {
      error.value = normalizeError(e).message
      return
    }
  }

  try {
    const resp = await axios.post(`/api/${provider}/login`, { code }, {
      headers: { 'Content-Type': 'application/json' },
    })

    if (resp.data.access_token) {
      setTokens(resp.data.access_token, resp.data.refresh_token)
      auth.markAuthenticated()
      await auth.fetchUser()
      router.replace('/')
    } else {
      error.value = t('oauth.noAccessToken', { provider: providerLabel })
    }
  } catch (e: unknown) {
    error.value = normalizeError(e).message
  }
})
</script>

<template>
  <div class="min-h-screen flex items-center justify-center">
    <div class="text-center max-w-md">
      <div
        v-if="error"
        class="rounded-lg bg-error-50 border border-error-200 p-4 text-sm text-error-700"
      >
        {{ error }}
      </div>
      <div v-else class="text-neutral-500">
        {{ $t('oauth.social.completing', { provider: providerLabel }) }}
      </div>
    </div>
  </div>
</template>
