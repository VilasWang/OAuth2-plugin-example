<script setup>
import { ref } from 'vue'
import { useRouter } from 'vue-router'

const router = useRouter()
const loading = ref(false)
const error = ref('')

const goToRegister = () => {
    router.push('/register')
}

const loginWithDrogon = async () => {
    loading.value = true
    error.value = ''
    localStorage.setItem('auth_provider', 'drogon')
    
    const state = crypto.randomUUID()
    localStorage.setItem('auth_state_drogon', state)
    
    const clientId = 'vue-client'
    const redirectUri = 'http://localhost:5173/callback'
    const scope = 'openid profile'
    const authUrl = `http://localhost:5555/oauth2/authorize?response_type=code&client_id=${clientId}&redirect_uri=${redirectUri}&scope=${scope}&state=${state}`

    try {
        const controller = new AbortController()
        const timeoutId = setTimeout(() => controller.abort(), 5000)

        await fetch(`http://localhost:5555/oauth2/authorize`, { 
            method: 'HEAD', 
            signal: controller.signal,
            mode: 'no-cors'
        })
        clearTimeout(timeoutId)
        
        window.location.href = authUrl
    } catch (err) {
        loading.value = false
        if (err.name === 'AbortError') {
            error.value = 'Connection timeout. Please ensure the backend server is running.'
        } else {
            error.value = `Unable to connect to authentication server.`
        }
    }
}

const loginWithWeChat = () => {
    localStorage.setItem('auth_provider', 'wechat')
    const state = crypto.randomUUID()
    localStorage.setItem('auth_state_wechat', state)
    
    const APPID = "YOUR_WECHAT_APPID"
    const REDIRECT_URI = encodeURIComponent("http://your-domain.com/callback")
    
    const url = `https://open.weixin.qq.com/connect/qrconnect?appid=${APPID}&redirect_uri=${REDIRECT_URI}&response_type=code&scope=snsapi_login&state=${state}#wechat_redirect`
    
    window.location.href = url
}

const loginWithGoogle = () => {
    localStorage.setItem('auth_provider', 'google')
    const state = crypto.randomUUID()
    localStorage.setItem('auth_state_google', state)
    
    const CLIENT_ID = "YOUR_GOOGLE_CLIENT_ID"
    const REDIRECT_URI = encodeURIComponent("http://localhost:5173/callback")
    const SCOPE = encodeURIComponent("openid email profile")
    
    const url = `https://accounts.google.com/o/oauth2/v2/auth?client_id=${CLIENT_ID}&redirect_uri=${REDIRECT_URI}&response_type=code&scope=${SCOPE}&state=${state}`
    
    window.location.href = url
}
</script>

<template>
  <div class="auth-page">
    <!-- Left Panel - Branding -->
    <div class="brand-panel">
      <div class="brand-content">
        <div class="logo">🔐</div>
        <h1>OAuth2 Platform</h1>
        <p class="tagline">Secure authentication for modern applications</p>
        
        <div class="features">
          <div class="feature">
            <span class="feature-icon">🛡️</span>
            <div>
              <strong>Enterprise Security</strong>
              <p>Industry-standard OAuth2.0 & OpenID Connect</p>
            </div>
          </div>
          <div class="feature">
            <span class="feature-icon">⚡</span>
            <div>
              <strong>Lightning Fast</strong>
              <p>Powered by Drogon high-performance C++ framework</p>
            </div>
          </div>
          <div class="feature">
            <span class="feature-icon">🔗</span>
            <div>
              <strong>Multi-Provider</strong>
              <p>Connect with Google, WeChat, and more</p>
            </div>
          </div>
        </div>
      </div>
    </div>

    <!-- Right Panel - Login Form -->
    <div class="form-panel">
      <div class="form-container">
        <div class="form-header">
          <h2>Welcome back</h2>
          <p>Sign in to continue to your account</p>
        </div>

        <!-- Error Alert -->
        <div v-if="error" class="alert alert-error">
          {{ error }}
        </div>

        <!-- Social Login Buttons -->
        <div class="social-buttons">
          <button @click="loginWithDrogon" :disabled="loading" class="btn btn-social btn-drogon">
            <span v-if="loading" class="spinner"></span>
            <span v-else>🚀</span>
            Sign in with Drogon
          </button>
          
          <button @click="loginWithGoogle" class="btn btn-social btn-google">
            <svg width="18" height="18" viewBox="0 0 24 24">
              <path fill="currentColor" d="M22.56 12.25c0-.78-.07-1.53-.2-2.25H12v4.26h5.92c-.26 1.37-1.04 2.53-2.21 3.31v2.77h3.57c2.08-1.92 3.28-4.74 3.28-8.09z"/>
              <path fill="currentColor" d="M12 23c2.97 0 5.46-.98 7.28-2.66l-3.57-2.77c-.98.66-2.23 1.06-3.71 1.06-2.86 0-5.29-1.93-6.16-4.53H2.18v2.84C3.99 20.53 7.7 23 12 23z"/>
              <path fill="currentColor" d="M5.84 14.09c-.22-.66-.35-1.36-.35-2.09s.13-1.43.35-2.09V7.07H2.18C1.43 8.55 1 10.22 1 12s.43 3.45 1.18 4.93l2.85-2.22.81-.62z"/>
              <path fill="currentColor" d="M12 5.38c1.62 0 3.06.56 4.21 1.64l3.15-3.15C17.45 2.09 14.97 1 12 1 7.7 1 3.99 3.47 2.18 7.07l3.66 2.84c.87-2.6 3.3-4.53 6.16-4.53z"/>
            </svg>
            Sign in with Google
          </button>
          
          <button @click="loginWithWeChat" class="btn btn-social btn-wechat">
            <svg width="20" height="20" viewBox="0 0 24 24">
              <path fill="currentColor" d="M8.691 2.188C3.891 2.188 0 5.476 0 9.53c0 2.212 1.17 4.203 3.002 5.55a.59.59 0 0 1 .213.665l-.39 1.48c-.019.07-.048.141-.048.213 0 .163.13.295.296.295a.32.32 0 0 0 .167-.054l1.903-1.114a.864.864 0 0 1 .717-.098 10.16 10.16 0 0 0 2.837.403c.276 0 .543-.027.811-.05a5.79 5.79 0 0 1-.271-1.752c0-3.293 2.931-5.964 6.548-5.964.343 0 .677.035 1.007.074-.64-2.93-3.793-5.19-7.564-5.19z"/>
            </svg>
            Sign in with WeChat
          </button>
        </div>

        <div class="divider">or</div>

        <!-- Register Link -->
        <div class="form-footer">
          <p>Don't have an account?</p>
          <button @click="goToRegister" class="btn btn-secondary" style="width: 100%">
            Create an account
          </button>
        </div>

        <p class="terms text-sm text-muted text-center mt-3">
          By signing in, you agree to our 
          <a href="#">Terms of Service</a> and 
          <a href="#">Privacy Policy</a>
        </p>
      </div>
    </div>
  </div>
</template>

<style scoped>
.auth-page {
  display: flex;
  min-height: 100vh;
}

/* Brand Panel */
.brand-panel {
  flex: 1;
  background: linear-gradient(135deg, rgba(102, 126, 234, 0.1) 0%, rgba(118, 75, 162, 0.1) 100%);
  display: flex;
  align-items: center;
  justify-content: center;
  padding: 2rem;
  position: relative;
  overflow: hidden;
}

.brand-panel::before {
  content: '';
  position: absolute;
  width: 500px;
  height: 500px;
  background: radial-gradient(circle, rgba(102, 126, 234, 0.2) 0%, transparent 70%);
  top: -200px;
  left: -200px;
}

.brand-panel::after {
  content: '';
  position: absolute;
  width: 400px;
  height: 400px;
  background: radial-gradient(circle, rgba(118, 75, 162, 0.15) 0%, transparent 70%);
  bottom: -150px;
  right: -150px;
}

.brand-content {
  position: relative;
  z-index: 1;
  max-width: 400px;
}

.brand-content .logo {
  margin-bottom: 1.5rem;
}

.brand-content h1 {
  font-size: 2.5rem;
  margin-bottom: 0.5rem;
  background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
  -webkit-background-clip: text;
  -webkit-text-fill-color: transparent;
  background-clip: text;
}

.tagline {
  font-size: 1.125rem;
  color: var(--text-secondary);
  margin-bottom: 3rem;
}

.features {
  display: flex;
  flex-direction: column;
  gap: 1.5rem;
}

.feature {
  display: flex;
  gap: 1rem;
  align-items: flex-start;
}

.feature-icon {
  font-size: 1.5rem;
  line-height: 1;
}

.feature strong {
  display: block;
  color: var(--text-primary);
  margin-bottom: 0.25rem;
}

.feature p {
  font-size: 0.875rem;
  color: var(--text-muted);
  margin: 0;
}

/* Form Panel */
.form-panel {
  flex: 1;
  display: flex;
  align-items: center;
  justify-content: center;
  padding: 2rem;
  background: var(--bg-secondary);
}

.form-container {
  width: 100%;
  max-width: 400px;
}

.form-header {
  text-align: center;
  margin-bottom: 2rem;
}

.form-header h2 {
  font-size: 1.75rem;
  margin-bottom: 0.5rem;
}

.form-header p {
  color: var(--text-muted);
}

.social-buttons {
  display: flex;
  flex-direction: column;
  gap: 0.75rem;
}

.form-footer {
  text-align: center;
}

.form-footer p {
  margin-bottom: 0.75rem;
  color: var(--text-muted);
}

.terms {
  margin-top: 2rem;
}

.terms a {
  color: var(--primary-color);
}

.terms a:hover {
  text-decoration: underline;
}

/* Responsive */
@media (max-width: 768px) {
  .auth-page {
    flex-direction: column;
  }
  
  .brand-panel {
    padding: 3rem 2rem;
  }
  
  .brand-content h1 {
    font-size: 2rem;
  }
  
  .features {
    display: none;
  }
}
</style>
