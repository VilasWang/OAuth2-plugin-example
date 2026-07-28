export interface User {
  sub: string
  name: string
  email?: string
  email_verified?: boolean
  mfa_enabled?: boolean
  roles?: string[]
}

export interface TokenResponse {
  access_token: string
  refresh_token: string
  token_type: string
  expires_in: number
  id_token?: string
}

export interface LoginResult {
  success?: boolean
  mfaRequired?: boolean
  mfaToken?: string
  error?: string
}

export interface AuthorizedApp {
  client_id: string
  name?: string
  scope?: string
  granted_at?: string
}

export interface UserProfile {
  username: string
  email: string
  email_verified: boolean
  mfa_enabled: boolean
  created_at?: string
}
