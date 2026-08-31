<script setup lang="ts">
import { ref, onMounted } from 'vue'
import http from '../../services/http'
import { userService } from '../../services/userService'
import { normalizeError } from '../../services/errorAdapter'
import { base64UrlEncode, base64UrlDecode } from '../../utils/pkce'
import type { SocialLink } from '../../types'
import AppAlert from '../../components/ui/AppAlert.vue'
import AppBadge from '../../components/ui/AppBadge.vue'
import AppCard from '../../components/ui/AppCard.vue'
import AppModal from '../../components/ui/AppModal.vue'
import DData from '../../components/ui/DData.vue'

const loading = ref(true)
const profile = ref<any>(null)
const success = ref('')
const error = ref('')

// Password change
const oldPassword = ref('')
const newPassword = ref('')
const confirmNewPassword = ref('')
const changingPassword = ref(false)

// MFA
const mfaSetupData = ref<any>(null)
const mfaVerifyCode = ref('')
const settingUpMfa = ref(false)
const disablingMfa = ref(false)
const disablePassword = ref('')
// One-time backup codes shown exactly once after MFA setup verification.
const backupCodes = ref<string[]>([])
const showBackupCodes = ref(false)

// Connected social accounts (B2 link/unlink)
const socialLinks = ref<SocialLink[]>([])
const socialLinksLoaded = ref(false)
const unlinkingProvider = ref('')
const linkingProvider = ref('')
const providerLabels: Record<string, string> = { github: 'GitHub', google: 'Google', wechat: 'WeChat' }

// #71: the link entry point goes through the server, which mints a one-time
// state bound to (user, provider) and returns the full authorize URL (the
// VITE_GITHUB_CLIENT_ID env dependency is gone).
async function beginSocialLink(provider: string) {
  if (linkingProvider.value) return
  linkingProvider.value = provider
  try {
    const { authorize_url: authorizeUrl } = await userService.beginSocialLink(provider)
    // Flow marker for the callback page (survives the full-page round trip
    // within this tab); the non-empty state in the callback is the backstop.
    sessionStorage.setItem('social_link_flow', provider)
    window.location.href = authorizeUrl
  } catch (e: unknown) {
    // Show the failure inline instead of leaving the card silently inert.
    window.alert(normalizeError(e).message)
  } finally {
    linkingProvider.value = ''
  }
}

async function fetchSocialLinks() {
  try {
    socialLinks.value = await userService.getSocialLinks()
  } catch {
    // The card shows its own empty state; a backend hiccup here must not
    // break the rest of the security page.
    socialLinks.value = []
  } finally {
    socialLinksLoaded.value = true
  }
}

async function unlinkSocial(provider: string) {
  const label = providerLabels[provider] || provider
  // W4: after unlinking, sign-in with this identity fails until it is
  // linked to an account again -- say so up front, not after the fact.
  if (!window.confirm(`Unlink your ${label} account? You will not be able to sign in with ${label} until it is linked to an account again.`)) return
  unlinkingProvider.value = provider
  try {
    await userService.unlinkSocialAccount(provider)
    showSuccess(`${label} account unlinked`)
    await fetchSocialLinks()
  } catch (e: unknown) {
    showError(normalizeError(e).message)
  } finally {
    unlinkingProvider.value = ''
  }
}

async function fetchProfile() {
  try {
    const resp = await http.get('/api/me')
    profile.value = resp.data
    if (webauthnSupported) fetchWebauthnCredentials()
  } catch {} finally { loading.value = false }
  fetchSocialLinks()
}

function showSuccess(msg: string) { success.value = msg; error.value = ''; setTimeout(() => { success.value = '' }, 4000) }
function showError(msg: string) { error.value = msg; success.value = '' }

async function changePassword() {
  if (newPassword.value !== confirmNewPassword.value) { showError('Passwords do not match'); return }
  if (newPassword.value.length < 8) { showError('Password must be at least 8 characters'); return }
  changingPassword.value = true
  try {
    await http.put('/api/me/password', { old_password: oldPassword.value, new_password: newPassword.value }, { headers: { 'Content-Type': 'application/json' } })
    showSuccess('Password changed successfully')
    oldPassword.value = ''; newPassword.value = ''; confirmNewPassword.value = ''
  } catch (e: unknown) {
    showError(normalizeError(e).message)
  } finally { changingPassword.value = false }
}

async function setupMfa() {
  settingUpMfa.value = true
  try {
    const resp = await http.post('/api/me/mfa/setup')
    mfaSetupData.value = resp.data
  } catch (e: unknown) {
    showError(normalizeError(e).message)
    settingUpMfa.value = false
  }
}

async function verifyMfaSetup() {
  try {
    const resp = await http.post('/api/me/mfa/verify', new URLSearchParams({ code: mfaVerifyCode.value }))
    // One-shot recovery codes: the backend returns 10 single-use backup codes
    // with an explicit "cannot be shown again" warning (MfaController). The
    // old code discarded them, leaving no self-service recovery path. Hold
    // the confirmation layer open until the user acknowledges saving.
    const codes: string[] = resp.data?.backup_codes || []
    mfaSetupData.value = null
    settingUpMfa.value = false
    mfaVerifyCode.value = ''
    if (codes.length > 0) {
      backupCodes.value = codes
      showBackupCodes.value = true
    } else {
      showSuccess('MFA enabled successfully!')
    }
    await fetchProfile()
  } catch (e: unknown) {
    showError(normalizeError(e).message)
  }
}

function dismissBackupCodes() {
  showBackupCodes.value = false
  backupCodes.value = []
  showSuccess('MFA enabled successfully!')
}

async function copyBackupCodes() {
  try {
    await navigator.clipboard.writeText(backupCodes.value.join('\n'))
  } catch {
    // Clipboard API unavailable (permissions/insecure context); the download
    // button remains as the fallback path.
  }
}

function downloadBackupCodes() {
  const blob = new Blob([backupCodes.value.join('\n')], { type: 'text/plain' })
  const url = URL.createObjectURL(blob)
  const a = document.createElement('a')
  a.href = url
  a.download = 'fulla-backup-codes.txt'
  a.click()
  URL.revokeObjectURL(url)
}

async function disableMfa() {
  if (!disablePassword.value) { showError('Password required to disable MFA'); return }
  disablingMfa.value = true
  try {
    await http.post('/api/me/mfa/disable', new URLSearchParams({ password: disablePassword.value }))
    showSuccess('MFA disabled')
    disablePassword.value = ''
    await fetchProfile()
  } catch (e: unknown) {
    showError(normalizeError(e).message)
  } finally { disablingMfa.value = false }
}

// Account deletion
const deleteConfirmUsername = ref('')
const deletingAccount = ref(false)

// WebAuthn / Passkeys
const webauthnCredentials = ref<any[]>([])
const registeringPasskey = ref(false)
const passkeyName = ref('')

async function fetchWebauthnCredentials() {
  try {
    const resp = await http.get('/api/me/webauthn/credentials')
    webauthnCredentials.value = resp.data.credentials || resp.data || []
  } catch {}
}

async function registerPasskey() {
  registeringPasskey.value = true
  try {
    // Step 1: Get creation options from the server. The payload nests the
    // PublicKeyCredentialCreationOptions under `options`, and the challenge
    // (and user.id) are base64url strings that must be decoded to bytes
    // before the browser API accepts them (atob throws on the -_ alphabet).
    const beginResp = await http.post('/api/me/webauthn/register/begin')
    const options = beginResp.data?.options
    if (!options?.challenge) throw new Error('Passkey registration: invalid server options')

    // Step 2: Call the browser WebAuthn API
    const credential = await navigator.credentials.create({
      publicKey: {
        challenge: base64UrlDecode(options.challenge),
        rp: options.rp || { name: 'Fulla', id: window.location.hostname },
        user: {
          id: base64UrlDecode(options.user?.id || base64UrlEncode(new TextEncoder().encode(profile.value?.username || 'user'))),
          name: options.user?.name || profile.value?.username || 'user',
          displayName: options.user?.displayName || profile.value?.username || 'User',
        },
        pubKeyCredParams: options.pubKeyCredParams || [{ alg: -7, type: 'public-key' }, { alg: -257, type: 'public-key' }],
        authenticatorSelection: options.authenticatorSelection || { userVerification: 'preferred' },
        timeout: options.timeout || 60000,
      }
    }) as PublicKeyCredential

    if (!credential) { showError('Passkey registration cancelled'); return }

    // Step 3: Send the credential to the server in ITS contract shape:
    // {credential_id, public_key, name} (base64url strings) — not the raw
    // browser attestation envelope. getPublicKey() (WebAuthn L3) yields the
    // SPKI DER public key; TS 5.9 lib.dom types it as ArrayBuffer | null.
    const attestationResponse = credential.response as AuthenticatorAttestationResponse
    const publicKeyBuffer = attestationResponse.getPublicKey()
    if (!publicKeyBuffer) {
      throw new Error('This browser did not expose the credential public key; passkey registration is not supported here')
    }
    const label = passkeyName.value.trim() || `Passkey ${new Date().toISOString().slice(0, 10)}`
    await http.post('/api/me/webauthn/register/finish', {
      credential_id: base64UrlEncode(new Uint8Array(credential.rawId)),
      public_key: base64UrlEncode(new Uint8Array(publicKeyBuffer)),
      name: label,
    }, { headers: { 'Content-Type': 'application/json' } })

    passkeyName.value = ''
    showSuccess('Passkey registered successfully!')
    await fetchWebauthnCredentials()
  } catch (e: any) {
    if (e.name === 'NotAllowedError') {
      showError('Passkey registration was cancelled or timed out')
    } else {
      showError(normalizeError(e).message)
    }
  } finally { registeringPasskey.value = false }
}

const webauthnSupported = typeof window !== 'undefined' && !!window.PublicKeyCredential

async function deleteAccount() {
  if (deleteConfirmUsername.value !== profile.value?.username) {
    showError('Username does not match. Please type your username to confirm.')
    return
  }
  deletingAccount.value = true
  try {
    await http.delete('/api/me')
    // Clear session and redirect to login
    localStorage.clear()
    window.location.href = '/login'
  } catch (e: unknown) {
    showError(normalizeError(e).message)
  } finally { deletingAccount.value = false }
}

onMounted(fetchProfile)
</script>

<template>
  <div>
    <h1 class="text-2xl font-bold text-neutral-900 mb-6">
      Security Settings
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
      Loading...
    </div>

    <div
      v-else
      class="space-y-6"
    >
      <!-- Change Password -->
      <AppCard>
        <h2 class="text-lg font-semibold text-neutral-900 mb-4">
          Change Password
        </h2>
        <form
          class="space-y-4 max-w-md"
          @submit.prevent="changePassword"
        >
          <div>
            <label class="block text-sm font-medium text-neutral-700 mb-1">Current Password</label>
            <input
              v-model="oldPassword"
              type="password"
              required
              autocomplete="current-password"
              class="block w-full px-3 py-2 border border-neutral-300 rounded-ctl text-sm focus:ring-2 focus:ring-brand-500"
            >
          </div>
          <div>
            <label class="block text-sm font-medium text-neutral-700 mb-1">New Password</label>
            <input
              v-model="newPassword"
              type="password"
              required
              autocomplete="new-password"
              class="block w-full px-3 py-2 border border-neutral-300 rounded-ctl text-sm focus:ring-2 focus:ring-brand-500"
            >
          </div>
          <div>
            <label class="block text-sm font-medium text-neutral-700 mb-1">Confirm New Password</label>
            <input
              v-model="confirmNewPassword"
              type="password"
              required
              autocomplete="new-password"
              class="block w-full px-3 py-2 border border-neutral-300 rounded-ctl text-sm focus:ring-2 focus:ring-brand-500"
            >
          </div>
          <button
            type="submit"
            :disabled="changingPassword"
            class="px-4 py-2 bg-brand-600 text-white rounded-ctl text-sm hover:bg-brand-700 disabled:opacity-50"
          >
            {{ changingPassword ? 'Changing...' : 'Change Password' }}
          </button>
        </form>
      </AppCard>

      <!-- MFA -->
      <AppCard>
        <h2 class="text-lg font-semibold text-neutral-900 mb-4">
          Two-Factor Authentication (MFA)
        </h2>

        <div
          v-if="profile?.mfa_enabled"
          class="space-y-4"
        >
          <div class="flex items-center gap-2">
            <AppBadge
              variant="success"
              size="sm"
            >Enabled</AppBadge>
            <p class="text-sm text-neutral-600">
              Your account is protected with TOTP-based MFA.
            </p>
          </div>
          <div class="border-t pt-4">
            <p class="text-sm text-neutral-600 mb-2">
              Enter your password to disable MFA:
            </p>
            <div class="flex gap-2 max-w-md">
              <input
                v-model="disablePassword"
                type="password"
                placeholder="Your password"
                class="flex-1 px-3 py-2 border border-neutral-300 rounded-ctl text-sm"
              >
              <button
                :disabled="disablingMfa"
                class="px-4 py-2 bg-error-600 text-white rounded-ctl text-sm hover:bg-error-700 disabled:opacity-50"
                @click="disableMfa"
              >
                {{ disablingMfa ? 'Disabling...' : 'Disable MFA' }}
              </button>
            </div>
          </div>
        </div>

        <div
          v-else-if="mfaSetupData"
          class="space-y-4"
        >
          <p class="text-sm text-neutral-600">
            Scan this QR code with your authenticator app (Google Authenticator, Authy, etc.):
          </p>
          <div class="bg-neutral-50 p-4 rounded-ctl text-center">
            <p class="text-xs text-neutral-500 mb-2">
              Manual entry key:
            </p>
            <code class="text-sm font-mono bg-surface px-3 py-1 rounded border select-all">{{ mfaSetupData.secret }}</code>
          </div>
          <div class="max-w-xs">
            <label class="block text-sm font-medium text-neutral-700 mb-1">Verification Code</label>
            <input
              v-model="mfaVerifyCode"
              type="text"
              inputmode="numeric"
              maxlength="6"
              class="block w-full px-3 py-2 border border-neutral-300 rounded-ctl text-sm text-center font-mono tabular-nums tracking-[0.42em]
                     focus:outline-none focus-visible:ring-[3px] focus-visible:ring-ring focus:border-brand-700"
              placeholder="000000"
            >
          </div>
          <div class="flex gap-2">
            <button
              :disabled="mfaVerifyCode.length !== 6"
              class="px-4 py-2 bg-brand-600 text-white rounded-ctl text-sm hover:bg-brand-700 disabled:opacity-50"
              @click="verifyMfaSetup"
            >
              Verify & Enable
            </button>
            <button
              class="px-4 py-2 border border-neutral-300 rounded-ctl text-sm hover:bg-neutral-50"
              @click="mfaSetupData = null; settingUpMfa = false"
            >
              Cancel
            </button>
          </div>
        </div>

        <div v-else>
          <p class="text-sm text-neutral-600 mb-4">
            Add an extra layer of security to your account with time-based one-time passwords (TOTP).
          </p>
          <button
            :disabled="settingUpMfa"
            class="px-4 py-2 bg-brand-600 text-white rounded-ctl text-sm hover:bg-brand-700 disabled:opacity-50"
            @click="setupMfa"
          >
            {{ settingUpMfa ? 'Setting up...' : 'Enable MFA' }}
          </button>
        </div>
      </AppCard>

      <!-- WebAuthn / Passkeys -->
      <AppCard v-if="webauthnSupported">
        <h2 class="text-lg font-semibold text-neutral-900 mb-4">
          Passkeys (WebAuthn)
        </h2>
        <p class="text-sm text-neutral-600 mb-4">
          Register a fingerprint, face, or security key to this account now —
          passkey sign-in becomes available once server-side assertion
          verification ships.
        </p>

        <!-- Registered credentials -->
        <div
          v-if="webauthnCredentials.length > 0"
          class="space-y-2 mb-4"
        >
          <div
            v-for="cred in webauthnCredentials"
            :key="cred.credential_id"
            class="flex items-center justify-between p-3 bg-neutral-50 rounded-ctl"
          >
            <div>
              <p class="text-sm font-medium text-neutral-900">
                {{ cred.name || 'Passkey' }}
              </p>
              <p class="text-xs text-neutral-500">
                Sign counter: {{ cred.sign_count ?? 0 }}
              </p>
            </div>
            <AppBadge
              variant="success"
              size="sm"
            >Active</AppBadge>
          </div>
        </div>
        <div
          v-else
          class="mb-4 p-3 bg-neutral-50 rounded-ctl text-sm text-neutral-500"
        >
          No passkeys registered yet.
        </div>

        <div class="flex flex-col sm:flex-row gap-2">
          <input
            v-model="passkeyName"
            type="text"
            placeholder="Passkey name (optional)"
            class="flex-1 px-3 py-2 text-sm rounded-ctl border border-neutral-300 focus:outline-none focus:ring-2 focus:ring-brand-500/20 focus:border-brand-500"
          >
          <button
            :disabled="registeringPasskey"
            class="px-4 py-2 bg-brand-600 text-white rounded-ctl text-sm hover:bg-brand-700 disabled:opacity-50 whitespace-nowrap"
            @click="registerPasskey"
          >
            {{ registeringPasskey ? 'Registering...' : '+ Add Passkey' }}
          </button>
        </div>
      </AppCard>

      <!-- Connected social accounts (B2 link/unlink) -->
      <AppCard>
        <h2 class="text-lg font-semibold text-neutral-900 mb-4">
          Connected Accounts
        </h2>
        <p class="text-sm text-neutral-600 mb-4">
          Link social identities to sign in with them. Unlinking removes the association but does not revoke active sessions.
        </p>

        <div
          v-if="socialLinksLoaded && socialLinks.length > 0"
          class="space-y-2 mb-4"
        >
          <div
            v-for="link in socialLinks"
            :key="link.provider"
            class="flex items-center justify-between p-3 bg-neutral-50 rounded-ctl"
          >
            <div>
              <p class="text-sm font-medium text-neutral-900">
                {{ providerLabels[link.provider] || link.provider }}
              </p>
              <p class="text-xs text-neutral-500">
                Linked {{ link.linked_at ? new Date(link.linked_at).toLocaleDateString() : '' }}
              </p>
            </div>
            <button
              :disabled="unlinkingProvider === link.provider"
              class="px-3 py-1.5 border border-error-200 text-error-600 rounded-ctl text-sm hover:bg-error-50 disabled:opacity-50"
              @click="unlinkSocial(link.provider)"
            >
              {{ unlinkingProvider === link.provider ? 'Unlinking...' : 'Unlink' }}
            </button>
          </div>
        </div>
        <div
          v-else-if="socialLinksLoaded"
          class="mb-4 p-3 bg-neutral-50 rounded-ctl text-sm text-neutral-500"
        >
          No social accounts linked.
        </div>

        <button
          v-if="!unlinkingProvider"
          :disabled="linkingProvider !== ''"
          class="inline-block px-4 py-2 bg-neutral-900 text-white rounded-ctl text-sm hover:bg-neutral-800 disabled:opacity-50"
          @click="beginSocialLink('github')"
        >
          {{ linkingProvider === 'github' ? 'Redirecting...' : 'Link GitHub Account' }}
        </button>
      </AppCard>

      <!-- Delete Account -->
      <div class="bg-surface rounded-card shadow-sm border border-error-200 p-6">
        <h2 class="text-lg font-semibold text-error-700 mb-2">
          Danger Zone
        </h2>
        <p class="text-sm text-neutral-600 mb-4">
          Permanently delete your account and all associated data. This action cannot be undone.
        </p>
        <div class="space-y-3 max-w-md">
          <div>
            <label class="block text-sm font-medium text-neutral-700 mb-1">
              Type <strong>{{ profile?.username }}</strong> to confirm
            </label>
            <input
              v-model="deleteConfirmUsername"
              type="text"
              autocomplete="off"
              class="block w-full px-3 py-2 border border-neutral-300 rounded-ctl text-sm focus:ring-2 focus:ring-error-500 focus:border-error-500"
              :placeholder="profile?.username"
            >
          </div>
          <button
            :disabled="deletingAccount || deleteConfirmUsername !== profile?.username"
            class="px-4 py-2 bg-error-600 text-white rounded-ctl text-sm hover:bg-error-700 disabled:opacity-50 disabled:cursor-not-allowed"
            @click="deleteAccount"
          >
            {{ deletingAccount ? 'Deleting...' : 'Delete My Account' }}
          </button>
        </div>
      </div>

      <!-- One-time MFA backup codes (gap-fix P0): the backend returns the 10
           single-use recovery codes exactly once and never again. The modal
           blocks until the user explicitly confirms saving them — AppModal's
           close (Esc/backdrop) is deliberately not wired for that reason. -->
      <AppModal
        :open="showBackupCodes"
        aria-label="Save your MFA backup codes"
        size="sm"
      >
        <h2 class="text-lg font-semibold text-neutral-900 mb-2">
          Save your backup codes
        </h2>
        <p class="text-sm text-error-600 font-medium mb-4">
          These 10 one-time codes are the only way to sign in if you lose your
          authenticator. They cannot be shown again.
        </p>
        <div class="grid grid-cols-2 gap-2 mb-4">
          <DData
            v-for="code in backupCodes"
            :key="code"
            :value="code"
            class="justify-center"
            data-testid="backup-code"
          />
        </div>
        <div class="flex gap-2 mb-4">
          <button
            type="button"
            class="flex-1 px-3 py-2 border border-neutral-300 rounded-ctl text-sm hover:bg-neutral-50
                   focus-visible:outline-none focus-visible:ring-[3px] focus-visible:ring-ring"
            @click="copyBackupCodes"
          >
            Copy all
          </button>
          <button
            type="button"
            class="flex-1 px-3 py-2 border border-neutral-300 rounded-ctl text-sm hover:bg-neutral-50
                   focus-visible:outline-none focus-visible:ring-[3px] focus-visible:ring-ring"
            @click="downloadBackupCodes"
          >
            Download .txt
          </button>
        </div>
        <button
          type="button"
          class="w-full px-4 py-2 bg-brand-600 text-white rounded-ctl text-sm hover:bg-brand-700
                 focus-visible:outline-none focus-visible:ring-[3px] focus-visible:ring-ring"
          @click="dismissBackupCodes"
        >
          I have safely saved my codes
        </button>
      </AppModal>
    </div>
  </div>
</template>

