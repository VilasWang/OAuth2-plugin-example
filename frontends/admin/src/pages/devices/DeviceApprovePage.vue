<script setup lang="ts">
import { ref } from 'vue'
import axios from 'axios'
import { useAuthStore } from '../../stores/auth'
import { normalizeError } from '../../services/errorAdapter'

// Gap-fix E2 / plan D5: device approval lives in the admin console because the
// backend endpoint is admin-gated (AuthorizationFilter + rbac rule). The user
// portal previously hosted a page calling a nonexistent endpoint.
//
// Contract (DeviceAuthController::approve): POST /oauth2/device/approve,
// form-urlencoded, required fields user_code + user_id; the Bearer token is
// attached by the store's axios request interceptor. Success: 200
// {status: "approved", user_code}.

const auth = useAuthStore()

const userCode = ref('')
const approving = ref(false)
const success = ref(false)
const errorMessage = ref('')

function normalizeCode(): string {
  return userCode.value.trim().toUpperCase()
}

async function approve() {
  const code = normalizeCode()
  if (!code) return
  approving.value = true
  success.value = false
  errorMessage.value = ''
  try {
    const resp = await axios.post('/oauth2/device/approve', new URLSearchParams({
      user_code: code,
      user_id: auth.user?.id || auth.user?.sub || '',
    }), {
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    })
    success.value = resp.data?.status === 'approved'
    if (success.value) {
      userCode.value = ''
    } else {
      errorMessage.value = 'The device code could not be approved'
    }
  } catch (e: unknown) {
    errorMessage.value = normalizeError(e).message
  } finally {
    approving.value = false
  }
}
</script>

<template>
  <div>
    <div class="mb-6">
      <h2 class="text-2xl font-bold text-neutral-900">
        Device Approval
      </h2>
      <p class="mt-1 text-sm text-neutral-500">
        Approve a device sign-in request by entering the code shown on the device screen.
      </p>
    </div>

    <div class="max-w-md bg-surface rounded-xl border border-neutral-200 shadow-sm p-6">
      <div
        v-if="success"
        class="rounded-lg bg-success-50 border border-success-200 p-4 mb-4"
        data-testid="device-approve-success"
      >
        <p class="text-sm font-medium text-success-700">
          Device approved
        </p>
        <p class="mt-1 text-sm text-success-700">
          You can close this page and return to your device.
        </p>
      </div>

      <div
        v-if="errorMessage"
        class="rounded-lg bg-error-50 border border-error-200 p-4 mb-4"
        data-testid="device-approve-error"
      >
        <p class="text-sm text-error-700">
          {{ errorMessage }}
        </p>
      </div>

      <form
        class="space-y-4"
        @submit.prevent="approve"
      >
        <div class="space-y-1.5">
          <label
            for="device-user-code"
            class="block text-sm font-medium text-neutral-700"
          >
            Device code
          </label>
          <input
            id="device-user-code"
            v-model="userCode"
            type="text"
            required
            autocomplete="off"
            placeholder="e.g. WDJB-MJHT"
            class="block w-full px-3.5 py-2.5 text-sm uppercase tracking-widest rounded-lg border border-neutral-300 bg-surface
                   placeholder:text-neutral-400 placeholder:tracking-normal transition-colors duration-150
                   focus:outline-none focus:ring-2 focus:ring-brand-500/20 focus:border-brand-700"
          >
        </div>

        <button
          type="submit"
          :disabled="approving || !normalizeCode()"
          class="w-full inline-flex items-center justify-center px-4 py-2.5 text-sm font-medium
                 bg-brand-600 text-white rounded-lg hover:bg-brand-700 shadow-sm
                 disabled:opacity-50 disabled:cursor-not-allowed
                 transition-all duration-150 active:scale-[0.98]"
        >
          {{ approving ? 'Approving...' : 'Approve device' }}
        </button>
      </form>
    </div>
  </div>
</template>
