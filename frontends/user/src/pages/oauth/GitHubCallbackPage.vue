<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { useRouter, useRoute } from 'vue-router'
import { useAuthStore } from '../../stores/auth'
import { setTokens, getAccessToken, tryRestoreSession } from '../../services/http'
import { userService } from '../../services/userService'
import { normalizeError } from '../../services/errorAdapter'
import axios from 'axios'

const router = useRouter()
const route = useRoute()
const auth = useAuthStore()
const error = ref('')

onMounted(async () => {
  const code = route.query.code as string
  if (!code) {
    error.value = 'No authorization code from GitHub'
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
      error.value = 'Missing link state; restart the link flow from the security page.'
      return
    }
    const hasSession = getAccessToken() ? true : await tryRestoreSession()
    if (!hasSession) {
      error.value = 'Please sign in first, then retry linking your GitHub account.'
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
      error.value = 'GitHub login did not return an access token'
    }
  } catch (e: unknown) {
    error.value = normalizeError(e).message
  }
})
</script>

<template>
  <div class="min-h-screen flex items-center justify-center">
    <div class="text-center max-w-md">
      <div v-if="error" class="p-6 bg-red-50 border border-red-200 rounded-lg">
        <p class="text-red-700 font-medium">GitHub Login Failed</p>
        <p class="text-red-600 text-sm mt-2">{{ error }}</p>
        <router-link to="/login" class="mt-4 inline-block text-indigo-600 hover:text-indigo-800">Back to Login</router-link>
      </div>
      <div v-else>
        <div class="animate-spin w-8 h-8 border-4 border-indigo-600 border-t-transparent rounded-full mx-auto"></div>
        <p class="mt-4 text-gray-600">Signing in with GitHub...</p>
      </div>
    </div>
  </div>
</template>
