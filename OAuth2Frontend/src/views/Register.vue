<script setup>
import { ref } from 'vue'
import { useRouter } from 'vue-router'

const router = useRouter()
const loading = ref(false)
const error = ref('')
const success = ref(false)
const countdown = ref(3)

const form = ref({
    username: '',
    email: '',
    password: '',
    confirmPassword: ''
})

const goToLogin = () => {
    router.push('/')
}

const validate = () => {
    if (!form.value.username || form.value.username.length < 3) {
        error.value = 'Username must be at least 3 characters'
        return false
    }
    if (!form.value.email || !form.value.email.includes('@')) {
        error.value = 'Please enter a valid email address'
        return false
    }
    if (!form.value.password || form.value.password.length < 6) {
        error.value = 'Password must be at least 6 characters'
        return false
    }
    if (form.value.password !== form.value.confirmPassword) {
        error.value = 'Passwords do not match'
        return false
    }
    return true
}

const startCountdown = () => {
    const timer = setInterval(() => {
        countdown.value--
        if (countdown.value <= 0) {
            clearInterval(timer)
            router.push('/')
        }
    }, 1000)
}

const register = async () => {
    error.value = ''
    success.value = false
    
    if (!validate()) return
    
    loading.value = true
    
    try {
        const response = await fetch('/api/register', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/x-www-form-urlencoded',
            },
            body: new URLSearchParams({
                username: form.value.username,
                email: form.value.email,
                password: form.value.password
            })
        })
        
        if (response.ok) {
            success.value = true
            countdown.value = 3
            startCountdown()
        } else {
            const text = await response.text()
            if (response.status === 409 || text.includes('exists')) {
                error.value = 'Username already exists. Please choose another.'
            } else {
                error.value = text || 'Registration failed. Please try again.'
            }
        }
    } catch (err) {
        error.value = 'Unable to connect to server. Please ensure the backend is running.'
    } finally {
        loading.value = false
    }
}
</script>

<template>
  <div class="auth-page">
    <!-- Left Panel - Branding -->
    <div class="brand-panel">
      <div class="brand-content">
        <div class="logo">🔐</div>
        <h1>Join OAuth2 Platform</h1>
        <p class="tagline">Create your account in seconds</p>
        
        <div class="benefits">
          <div class="benefit">
            <span class="check">✓</span>
            Free to use, forever
          </div>
          <div class="benefit">
            <span class="check">✓</span>
            Secure authentication
          </div>
          <div class="benefit">
            <span class="check">✓</span>
            Connect multiple providers
          </div>
          <div class="benefit">
            <span class="check">✓</span>
            No credit card required
          </div>
        </div>
      </div>
    </div>

    <!-- Right Panel - Register Form -->
    <div class="form-panel">
      <div class="form-container">
        <div class="form-header">
          <h2>Create account</h2>
          <p>Start your journey with us</p>
        </div>

        <!-- Error Alert -->
        <div v-if="error" class="alert alert-error">
          {{ error }}
        </div>

        <!-- Success Overlay -->
        <Transition name="success-fade">
          <div v-if="success" class="success-overlay">
            <div class="success-card">
              <div class="success-icon">
                <svg width="64" height="64" viewBox="0 0 24 24" fill="none">
                  <circle cx="12" cy="12" r="11" stroke="#48bb78" stroke-width="2"/>
                  <path d="M7 12.5l3 3 7-7" stroke="#48bb78" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>
                </svg>
              </div>
              <h3>Account Created!</h3>
              <p>Welcome to OAuth2 Platform</p>
              <div class="countdown-text">
                Redirecting to login in <span class="countdown-number">{{ countdown }}</span> seconds...
              </div>
              <div class="progress-bar">
                <div class="progress-fill" :style="{ width: ((3 - countdown) / 3 * 100) + '%' }"></div>
              </div>
            </div>
          </div>
        </Transition>

        <form @submit.prevent="register" class="register-form">
          <div class="form-group">
            <label for="username">Username</label>
            <input 
              type="text" 
              id="username" 
              v-model="form.username"
              placeholder="Choose a username"
              required
              autocomplete="username"
            />
          </div>

          <div class="form-group">
            <label for="email">Email</label>
            <input 
              type="email" 
              id="email" 
              v-model="form.email"
              placeholder="you@example.com"
              required
              autocomplete="email"
            />
          </div>

          <div class="form-group">
            <label for="password">Password</label>
            <input 
              type="password" 
              id="password" 
              v-model="form.password"
              placeholder="Create a strong password"
              required
              autocomplete="new-password"
            />
          </div>

          <div class="form-group">
            <label for="confirmPassword">Confirm Password</label>
            <input 
              type="password" 
              id="confirmPassword" 
              v-model="form.confirmPassword"
              placeholder="Confirm your password"
              required
              autocomplete="new-password"
            />
          </div>

          <button type="submit" class="btn btn-primary" :disabled="loading" style="width: 100%">
            <span v-if="loading" class="spinner"></span>
            <span v-else>Create account</span>
          </button>
        </form>

        <div class="divider">or</div>

        <div class="form-footer">
          <p>Already have an account?</p>
          <button @click="goToLogin" class="btn btn-secondary" style="width: 100%">
            Sign in instead
          </button>
        </div>

        <p class="terms text-sm text-muted text-center mt-3">
          By creating an account, you agree to our 
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
  right: -200px;
}

.brand-panel::after {
  content: '';
  position: absolute;
  width: 400px;
  height: 400px;
  background: radial-gradient(circle, rgba(118, 75, 162, 0.15) 0%, transparent 70%);
  bottom: -150px;
  left: -150px;
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
  font-size: 2.25rem;
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

.benefits {
  display: flex;
  flex-direction: column;
  gap: 1rem;
}

.benefit {
  display: flex;
  align-items: center;
  gap: 0.75rem;
  color: var(--text-secondary);
}

.check {
  width: 24px;
  height: 24px;
  background: rgba(72, 187, 120, 0.2);
  color: #48bb78;
  border-radius: 50%;
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 0.75rem;
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

.register-form {
  display: flex;
  flex-direction: column;
  gap: 1.25rem;
}

.form-group {
  display: flex;
  flex-direction: column;
  gap: 0.5rem;
}

.form-group label {
  font-size: 0.875rem;
  font-weight: 500;
  color: var(--text-secondary);
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
    font-size: 1.75rem;
  }
  
  .benefits {
    display: none;
  }
}

/* Success Overlay */
.success-overlay {
  position: fixed;
  top: 0;
  left: 0;
  right: 0;
  bottom: 0;
  background: rgba(15, 15, 35, 0.95);
  backdrop-filter: blur(10px);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 1000;
}

.success-card {
  background: var(--bg-card);
  border: 1px solid var(--border-color);
  border-radius: 16px;
  padding: 3rem;
  text-align: center;
  max-width: 400px;
  animation: successPop 0.4s ease-out;
}

@keyframes successPop {
  0% {
    opacity: 0;
    transform: scale(0.8);
  }
  50% {
    transform: scale(1.05);
  }
  100% {
    opacity: 1;
    transform: scale(1);
  }
}

.success-icon {
  margin-bottom: 1.5rem;
  animation: checkDraw 0.6s ease-out 0.2s both;
}

@keyframes checkDraw {
  0% {
    opacity: 0;
    transform: scale(0.5) rotate(-10deg);
  }
  100% {
    opacity: 1;
    transform: scale(1) rotate(0deg);
  }
}

.success-icon svg {
  filter: drop-shadow(0 0 20px rgba(72, 187, 120, 0.5));
}

.success-card h3 {
  font-size: 1.5rem;
  color: #48bb78;
  margin-bottom: 0.5rem;
}

.success-card p {
  color: var(--text-secondary);
  margin-bottom: 1.5rem;
}

.countdown-text {
  color: var(--text-muted);
  font-size: 0.875rem;
  margin-bottom: 1rem;
}

.countdown-number {
  display: inline-block;
  width: 1.5em;
  font-weight: 600;
  color: var(--primary-color);
  font-size: 1.1rem;
}

.progress-bar {
  height: 4px;
  background: var(--bg-input);
  border-radius: 2px;
  overflow: hidden;
}

.progress-fill {
  height: 100%;
  background: linear-gradient(90deg, #667eea, #48bb78);
  border-radius: 2px;
  transition: width 1s linear;
}

/* Transition */
.success-fade-enter-active {
  animation: fadeIn 0.3s ease-out;
}

.success-fade-leave-active {
  animation: fadeOut 0.3s ease-in;
}

@keyframes fadeIn {
  from { opacity: 0; }
  to { opacity: 1; }
}

@keyframes fadeOut {
  from { opacity: 1; }
  to { opacity: 0; }
}
</style>
