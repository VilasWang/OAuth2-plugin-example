import http, { setTokens, clearTokens, getAccessToken } from './http'
import type { LoginResult, TokenResponse } from '../types'

const CLIENT_ID = import.meta.env.VITE_CLIENT_ID || 'vue-client'
const REDIRECT_URI = import.meta.env.VITE_REDIRECT_URI || window.location.origin + '/callback'

export const authService = {
  async login(username: string, password: string, scope = 'openid profile email'): Promise<LoginResult> {
    const resp = await http.post('/oauth2/login', new URLSearchParams({
      username, password,
      client_id: CLIENT_ID,
      redirect_uri: REDIRECT_URI,
      scope,
      state: crypto.randomUUID(),
      json: 'true',
    }))

    if (resp.data.mfa_required) {
      return { mfaRequired: true, mfaToken: resp.data.mfa_token }
    }

    const code = resp.data.code
    if (!code) throw new Error('No authorization code received')

    const tokenResp = await http.post<TokenResponse>('/oauth2/token', new URLSearchParams({
      grant_type: 'authorization_code',
      code,
      redirect_uri: REDIRECT_URI,
      client_id: CLIENT_ID,
    }))

    setTokens(tokenResp.data.access_token, tokenResp.data.refresh_token)
    return { success: true }
  },

  async verifyMfa(mfaToken: string, code: string): Promise<LoginResult> {
    const resp = await http.post<TokenResponse>('/oauth2/mfa/verify', new URLSearchParams({
      mfa_token: mfaToken,
      code,
      client_id: CLIENT_ID,
      redirect_uri: REDIRECT_URI,
    }))
    if (resp.data.access_token) {
      setTokens(resp.data.access_token, resp.data.refresh_token)
      return { success: true }
    }
    return { error: 'MFA verification failed' }
  },

  async exchangeCode(code: string): Promise<void> {
    const resp = await http.post<TokenResponse>('/oauth2/token', new URLSearchParams({
      grant_type: 'authorization_code',
      code,
      redirect_uri: REDIRECT_URI,
      client_id: CLIENT_ID,
    }))
    setTokens(resp.data.access_token, resp.data.refresh_token)
  },

  async register(username: string, password: string, email: string): Promise<void> {
    await http.post('/api/register', new URLSearchParams({ username, password, email }))
  },

  async requestPasswordReset(email: string): Promise<void> {
    await http.post('/api/password-reset/request', JSON.stringify({ email }), {
      headers: { 'Content-Type': 'application/json' },
    })
  },

  async confirmPasswordReset(token: string, newPassword: string): Promise<void> {
    await http.post('/api/password-reset/confirm', JSON.stringify({ token, new_password: newPassword }), {
      headers: { 'Content-Type': 'application/json' },
    })
  },

  async logout(): Promise<void> {
    try {
      const token = getAccessToken()
      if (token) {
        await http.post('/oauth2/revoke', new URLSearchParams({
          token,
          client_id: CLIENT_ID,
        }))
      }
    } catch {} finally {
      clearTokens()
    }
  },
}
