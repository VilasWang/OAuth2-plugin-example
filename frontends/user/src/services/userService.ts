import http from './http'
import type { User, UserProfile, AuthorizedApp } from '../types'

export const userService = {
  async getUserInfo(): Promise<User> {
    const resp = await http.get<User>('/oauth2/userinfo')
    return resp.data
  },

  async getProfile(): Promise<UserProfile> {
    const resp = await http.get<UserProfile>('/api/me')
    return resp.data
  },

  async changePassword(oldPassword: string, newPassword: string): Promise<void> {
    await http.put('/api/me/password', JSON.stringify({ old_password: oldPassword, new_password: newPassword }), {
      headers: { 'Content-Type': 'application/json' },
    })
  },

  async setupMfa(): Promise<{ secret: string; qr_uri?: string }> {
    const resp = await http.post('/api/me/mfa/setup')
    return resp.data
  },

  async verifyMfaSetup(code: string): Promise<void> {
    await http.post('/api/me/mfa/verify', new URLSearchParams({ code }))
  },

  async disableMfa(password: string): Promise<void> {
    await http.post('/api/me/mfa/disable', new URLSearchParams({ password }))
  },

  async getAuthorizedApps(): Promise<AuthorizedApp[]> {
    const resp = await http.get('/api/me/authorized-apps')
    return resp.data.apps || resp.data || []
  },

  async revokeApp(clientId: string): Promise<void> {
    await http.delete(`/api/me/authorized-apps/${clientId}`)
  },

  async resendVerificationEmail(): Promise<void> {
    await http.post('/api/verify-email/resend')
  },

  async verifyEmail(token: string): Promise<string> {
    const resp = await http.get(`/api/verify-email?token=${encodeURIComponent(token)}`)
    return resp.data?.message || 'Email verified'
  },
}
