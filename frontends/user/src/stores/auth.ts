import { defineStore } from 'pinia'
import { ref, computed } from 'vue'
import { authService } from '../services/authService'
import { userService } from '../services/userService'
import { getAccessToken, getRefreshToken, clearTokens as httpClearTokens, tryRestoreSession } from '../services/http'
import { normalizeError } from '../services/errorAdapter'
import type { User, LoginResult } from '../types'

export const useAuthStore = defineStore('auth', () => {
  const user = ref<User | null>(null)
  const loading = ref(false)
  const error = ref('')
  const tokenPresent = ref(!!getAccessToken() || !!getRefreshToken())
  const sessionRestored = ref(false)

  const isAuthenticated = computed(() => tokenPresent.value)

  function markAuthenticated() { tokenPresent.value = true }
  function markUnauthenticated() { tokenPresent.value = false }

  /** Restore session from refresh_token on page reload */
  async function restoreSession(): Promise<boolean> {
    if (sessionRestored.value) return !!getAccessToken()
    sessionRestored.value = true
    if (getAccessToken()) return true
    if (!getRefreshToken()) { markUnauthenticated(); return false }
    const restored = await tryRestoreSession()
    if (restored) {
      markAuthenticated()
      await fetchUser()
    } else {
      markUnauthenticated()
    }
    return restored
  }

  async function login(username: string, password: string): Promise<LoginResult> {
    error.value = ''
    loading.value = true
    try {
      const result = await authService.login(username, password)
      if (result.success) {
        markAuthenticated()
        await fetchUser()
      }
      return result
    } catch (e: unknown) {
      error.value = normalizeError(e).message
      return { error: error.value }
    } finally {
      loading.value = false
    }
  }

  async function verifyMfa(mfaToken: string, code: string): Promise<LoginResult> {
    error.value = ''
    loading.value = true
    try {
      const result = await authService.verifyMfa(mfaToken, code)
      if (result.success) {
        markAuthenticated()
        await fetchUser()
      }
      return result
    } catch (e: unknown) {
      error.value = normalizeError(e).message
      return { error: error.value }
    } finally {
      loading.value = false
    }
  }

  async function exchangeCode(code: string) {
    await authService.exchangeCode(code)
    markAuthenticated()
    await fetchUser()
  }

  async function fetchUser() {
    try {
      user.value = await userService.getUserInfo()
    } catch {
      user.value = null
    }
  }

  async function logout() {
    await authService.logout()
    user.value = null
    httpClearTokens()
    markUnauthenticated()
  }

  // Initialize: try to restore session if refresh_token exists
  if (getRefreshToken()) {
    restoreSession()
  }

  return { user, loading, error, isAuthenticated, login, verifyMfa, exchangeCode, fetchUser, logout, restoreSession, markAuthenticated }
})
