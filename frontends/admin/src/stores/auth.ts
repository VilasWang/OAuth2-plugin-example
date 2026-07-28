import { defineStore } from 'pinia'
import { ref, computed } from 'vue'
import axios from 'axios'
import { normalizeError, sessionExpiredError } from '../services/errorAdapter'
import type { NormalizedError } from '../services/errorAdapter'

// Refresh token is persisted in sessionStorage so a page refresh can restore the
// session without re-prompting credentials. The access token stays in memory only
// (never persisted), limiting exposure to XSS. sessionStorage is scoped to the
// tab and cleared when the tab closes. See A-LOGIN-014 / A-SEC-002.
const REFRESH_TOKEN_STORAGE_KEY = 'admin_refresh_token'

function persistRefreshToken(token: string | null) {
  try {
    if (token) {
      sessionStorage.setItem(REFRESH_TOKEN_STORAGE_KEY, token)
    } else {
      sessionStorage.removeItem(REFRESH_TOKEN_STORAGE_KEY)
    }
  } catch {
    // sessionStorage may be unavailable (private mode, disabled); degrade to
    // in-memory only — login still works for the lifetime of the tab.
  }
}

function loadPersistedRefreshToken(): string | null {
  try {
    return sessionStorage.getItem(REFRESH_TOKEN_STORAGE_KEY)
  } catch {
    return null
  }
}

// Shared promise so the router guard, the app bootstrap, and any component
// mounting during the first tick all wait for the same one-shot session
// restoration. Without this, the initial navigation can run before
// restoreSession() finishes, see isAuthenticated === false, and bounce to
// /login even though the refresh token is valid. See A-LOGIN-014.
let sessionRestorePromise: Promise<boolean> | null = null

/**
 * Navigate to the login view after a failed 401 token refresh
 * (Requirement 10.5).
 *
 * Uses a lazy dynamic import of the router to avoid a static import cycle
 * (router → stores/auth → router). Falls back to a hard redirect if the
 * router cannot be resolved (e.g. very early during bootstrap).
 */
async function redirectToLogin(): Promise<void> {
  try {
    const { default: router } = await import('../router')
    if (router.currentRoute.value.name !== 'login') {
      await router.push({ name: 'login' })
    }
  } catch {
    window.location.href = '/admin/login'
  }
}

export const useAuthStore = defineStore('auth', () => {
  const accessToken = ref<string | null>(null)
  const refreshToken = ref<string | null>(null)
  const user = ref<any>(null)
  const loginError = ref('')

  const isAuthenticated = computed(() => !!accessToken.value)

  /**
   * Direct login: POST credentials to /oauth2/login (json mode)
   * Then exchange the code for tokens.
   * This avoids the redirect-based flow for the admin SPA.
   */
  async function login(username: string, password: string) {
    loginError.value = ''
    try {
      // Step 1: Login and get auth code
      const loginResp = await axios.post('/oauth2/login', new URLSearchParams({
        username,
        password,
        client_id: 'admin-console',
        redirect_uri: window.location.origin + '/admin/callback',
        scope: 'openid profile admin',
        state: crypto.randomUUID(),
        json: 'true',
      }), {
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      })

      if (loginResp.data.mfa_required) {
        // MFA required - return the mfa_token for the MFA step
        return { mfaRequired: true, mfaToken: loginResp.data.mfa_token }
      }

      const code = loginResp.data.code
      if (!code) {
        throw new Error('No authorization code received')
      }

      // Step 2: Exchange code for tokens
      const tokenResp = await axios.post('/oauth2/token', new URLSearchParams({
        grant_type: 'authorization_code',
        code,
        redirect_uri: window.location.origin + '/admin/callback',
        client_id: 'admin-console',
      }), {
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      })

      accessToken.value = tokenResp.data.access_token
      refreshToken.value = tokenResp.data.refresh_token
      persistRefreshToken(refreshToken.value)

      // Step 3: Fetch user info
      await fetchUserInfo()

      // Verify admin role
      if (!user.value?.roles?.includes('admin')) {
        logout()
        loginError.value = 'Admin role required to access this console'
        return { error: 'forbidden' }
      }

      return { success: true }
    } catch (e: unknown) {
      // Surface a consistent localized message via the Frontend_Error_Module
      // instead of reading raw e.response.data.* (Requirements 10.2, 10.3).
      loginError.value = normalizeError(e).message
      return { error: loginError.value }
    }
  }

  async function fetchUserInfo() {
    if (!accessToken.value) return
    try {
      const response = await axios.get('/oauth2/userinfo', {
        headers: { Authorization: `Bearer ${accessToken.value}` },
      })
      user.value = response.data
    } catch {
      logout()
    }
  }

  // In-flight refresh promise. When multiple requests 401 concurrently (common
  // on page load: userinfo + dashboard + list endpoints), all of them must share
  // a single refresh_token exchange — the backend rotates the refresh token on
  // each use, so a second concurrent exchange would hit the now-invalidated
  // token and log the user out. See A-LOGIN-014.
  let refreshInFlight: Promise<boolean> | null = null

  async function refreshAccessToken() {
    if (!refreshToken.value) return false
    if (refreshInFlight) return refreshInFlight
    refreshInFlight = (async () => {
      try {
        const resp = await axios.post('/oauth2/token', new URLSearchParams({
          grant_type: 'refresh_token',
          refresh_token: refreshToken.value,
          client_id: 'admin-console',
        }), {
          headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        })
        accessToken.value = resp.data.access_token
        refreshToken.value = resp.data.refresh_token
        persistRefreshToken(refreshToken.value)
        return true
      } catch {
        logout()
        return false
      } finally {
        refreshInFlight = null
      }
    })()
    return refreshInFlight
  }

  function logout() {
    accessToken.value = null
    refreshToken.value = null
    user.value = null
    persistRefreshToken(null)
  }

  /**
   * Restore a previously established session after a page refresh.
   * Uses the persisted refresh token to obtain a fresh access token. If the
   * refresh fails (expired/revoked), the session is cleared silently and the
   * user is treated as unauthenticated. Returns true if the session was
   * restored. See A-LOGIN-014.
   */
  async function restoreSession(): Promise<boolean> {
    const persisted = loadPersistedRefreshToken()
    if (!persisted) return false
    refreshToken.value = persisted
    const ok = await refreshAccessToken()
    if (!ok) return false
    await fetchUserInfo()
    // Verify the restored session still has the admin role.
    return !!user.value?.roles?.includes('admin')
  }

  // Deduplicate session restoration across concurrent callers (router guard +
  // app bootstrap). Returns a shared promise; the underlying restoreSession
  // runs at most once per page load. See A-LOGIN-014.
  function ensureSessionRestored(): Promise<boolean> {
    if (!sessionRestorePromise) {
      sessionRestorePromise = restoreSession()
    }
    return sessionRestorePromise
  }

  // Axios interceptor for auto-attaching token
  axios.interceptors.request.use((config) => {
    if (accessToken.value && !config.headers.Authorization) {
      config.headers.Authorization = `Bearer ${accessToken.value}`
    }
    return config
  })

  // Axios interceptor for 401 → auto refresh, else normalize the error so
  // views always receive a NormalizedError (Requirements 10.2, 10.3, 10.4, 10.5).
  axios.interceptors.response.use(
    (response) => response,
    async (error) => {
      const originalRequest = error.config
      if (
        error.response?.status === 401 &&
        refreshToken.value &&
        originalRequest &&
        !originalRequest._retry
      ) {
        originalRequest._retry = true
        const refreshed = await refreshAccessToken()
        if (refreshed) {
          originalRequest.headers.Authorization = `Bearer ${accessToken.value}`
          return axios(originalRequest)
        }
        // Token refresh failed: clear the session, surface a consistent
        // localized "session expired" message via the Frontend_Error_Module,
        // and navigate to the login view (Requirements 10.4, 10.5). The
        // rejected value is a BRANDED NormalizedError so a view calling
        // normalizeError(e) receives it unchanged (idempotent passthrough).
        logout()
        const expired: NormalizedError = sessionExpiredError()
        loginError.value = expired.message
        redirectToLogin()
        return Promise.reject(expired)
      }
      // All other errors: reject the raw axios error untouched so each view
      // parses it through the Frontend_Error_Module via normalizeError(e),
      // displaying a consistent localized message without reading raw
      // e.response.data.* (Requirements 10.2, 10.3).
      return Promise.reject(error)
    }
  )

  return {
    accessToken,
    user,
    loginError,
    isAuthenticated,
    login,
    fetchUserInfo,
    refreshAccessToken,
    restoreSession,
    ensureSessionRestored,
    logout,
  }
})
