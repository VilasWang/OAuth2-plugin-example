<script setup lang="ts">
import { ref } from 'vue'
import { useRoute } from 'vue-router'
import { useAuthStore } from '../../stores/auth'

const route = useRoute()
const auth = useAuthStore()
const loading = ref(false)

const clientId = route.query.client_id as string || ''
const scope = route.query.scope as string || ''
const redirectUri = route.query.redirect_uri as string || ''
const state = route.query.state as string || ''
// Gap-fix E7: the server's consent page URL explicitly carries the session
// user's id (AuthorizationEndpointController). Trust that value first — the
// store's userinfo may not have loaded yet (no auth meta on this route before
// the guard was added), and `sub` used to be silently submitted as ''.
const serverUserId = route.query.user_id as string || ''
const userId = serverUserId || auth.user?.sub || ''
// P0 #1: PKCE/nonce must round-trip through the consent page, otherwise the
// authorization code is issued without a stored code_challenge and the token
// endpoint's PKCE verification is silently skipped.
const codeChallenge = route.query.code_challenge as string || ''
// F1: server-minted CSRF nonce from the authorize->consent redirect; the
// consent POST must echo it (one-shot, TTL-bounded server-side).
const consentCsrf = route.query.consent_csrf as string || ''
const codeChallengeMethod = route.query.code_challenge_method as string || ''
const nonce = route.query.nonce as string || ''

const scopes = scope.split(' ').filter(Boolean)

// Neither the server-provided nor the store user id is known — submitting
// would 500 server-side ("failed to get user mapping"). Block the action
// instead of firing a request that cannot succeed.
const missingUserId = !userId

// #43: resource-scope vocabulary. The legacy bare 'read'/'write' labels are
// dropped; the OIDC standard scopes keep their human-readable descriptions and
// the resource-prefixed admin scopes are listed for completeness (they
// normally appear only for admin-console clients, not end users).
const scopeDescriptions: Record<string, string> = {
  openid: 'Verify your identity',
  profile: 'Access your basic profile (username)',
  email: 'Access your email address',
  admin: 'Administrative access',
  'users:read': 'Read user accounts',
  'users:write': 'Manage user accounts',
  'clients:read': 'View registered applications',
  'clients:write': 'Manage registered applications',
  'tokens:read': 'View active tokens',
  'tokens:write': 'Revoke tokens',
  'roles:read': 'View roles and scopes',
  'roles:write': 'Manage roles and scopes',
  'audit:read': 'View audit logs and statistics',
}

/**
 * Submit consent via a native form POST (not XHR).
 *
 * The backend returns a real HTTP 302 redirect to the client's redirect_uri
 * with ?code=... (F-020). An XHR/fetch would auto-follow this redirect and
 * land on an opaque cross-origin response, preventing the browser from
 * navigating. A native form POST lets the browser handle the 302 directly,
 * correctly redirecting the top-level window to the client application.
 * This is the standard browser pattern for OAuth2 consent submission.
 */
function handleConsent(action: 'approve' | 'deny') {
  loading.value = true
  const form = document.createElement('form')
  form.method = 'POST'
  form.action = '/oauth2/consent'
  form.enctype = 'application/x-www-form-urlencoded'

  const fields: Record<string, string> = {
    client_id: clientId,
    user_id: userId,
    scope,
    redirect_uri: redirectUri,
    state,
    action,
  }
  if (consentCsrf) {
    fields.consent_csrf = consentCsrf
  }
  if (codeChallenge) {
    fields.code_challenge = codeChallenge
    fields.code_challenge_method = codeChallengeMethod
  }
  if (nonce) fields.nonce = nonce

  for (const [key, value] of Object.entries(fields)) {
    const input = document.createElement('input')
    input.type = 'hidden'
    input.name = key
    input.value = value
    form.appendChild(input)
  }
  document.body.appendChild(form)
  form.submit()
}
</script>

<template>
  <div class="min-h-screen flex items-center justify-center bg-gradient-to-br from-brand-50 to-brand-100 px-4">
    <div class="w-full max-w-md bg-surface rounded-2xl shadow-xl p-8">
      <div class="text-center mb-6">
        <h1 class="text-2xl font-bold text-neutral-900">
          Authorize Application
        </h1>
        <p class="mt-2 text-neutral-500">
          <strong class="text-neutral-700">{{ clientId }}</strong> is requesting access to your account
        </p>
      </div>

      <!-- Requested Permissions -->
      <div class="bg-neutral-50 rounded-lg p-4 mb-6">
        <p class="text-sm font-medium text-neutral-700 mb-3">
          This application will be able to:
        </p>
        <ul class="space-y-2">
          <li
            v-for="s in scopes"
            :key="s"
            class="flex items-center gap-2 text-sm text-neutral-600"
          >
            <span class="w-5 h-5 bg-brand-100 text-brand-600 rounded-full flex items-center justify-center text-xs">&#10003;</span>
            {{ scopeDescriptions[s] || s }}
          </li>
        </ul>
      </div>

      <!-- Signed in as -->
      <div class="text-center text-sm text-neutral-500 mb-6">
        Signed in as <strong>{{ auth.user?.name || auth.user?.sub }}</strong>
      </div>

      <!-- Actions -->
      <p
        v-if="missingUserId"
        class="mb-3 text-center text-sm text-error-600"
        data-testid="consent-missing-user"
      >
        Your session could not be identified. Please sign in again.
      </p>
      <div class="flex gap-3">
        <button
          :disabled="loading || missingUserId"
          class="flex-1 py-3 border border-neutral-300 text-neutral-700 font-medium rounded-lg hover:bg-neutral-50 disabled:opacity-50"
          @click="handleConsent('deny')"
        >
          Deny
        </button>
        <button
          :disabled="loading || missingUserId"
          class="flex-1 py-3 bg-brand-600 text-white font-medium rounded-lg hover:bg-brand-700 disabled:opacity-50"
          @click="handleConsent('approve')"
        >
          {{ loading ? 'Authorizing...' : 'Authorize' }}
        </button>
      </div>
    </div>
  </div>
</template>
