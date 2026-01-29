<script setup>
import { onMounted, ref } from 'vue'
import { useRoute, useRouter } from 'vue-router'

const route = useRoute()
const router = useRouter()
const status = ref('Processing...')
const userInfo = ref(null)
const error = ref(null)

onMounted(async () => {
    const code = route.query.code;
    const state = route.query.state;

    if (!code) {
        error.value = "No authorization code found in URL";
        return;
    }

    const provider = localStorage.getItem('auth_provider') || 'drogon';
    status.value = `Verifying ${provider} Login...`;
    
    try {
        if (provider === 'wechat' || provider === 'google') {
            // Validate state for external providers
            const savedState = localStorage.getItem(`auth_state_${provider}`);
            if (state !== savedState) {
                throw new Error("Invalid state parameter (CSRF Protection)");
            }
            localStorage.removeItem(`auth_state_${provider}`);
            
            // Call our Backend to perform the server-side exchange for external providers
            const loginEndpoint = provider === 'wechat' ? '/api/wechat/login' : '/api/google/login';
            
            const response = await fetch(`/api${loginEndpoint}`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                body: new URLSearchParams({ code: code })
            });

            if (!response.ok) {
                const errText = await response.text();
                throw new Error(`${provider} Login Failed: ${errText}`);
            }

            userInfo.value = await response.json();
            
            // Normalize display fields
            if (provider === 'wechat') {
                userInfo.value.displayName = userInfo.value.nickname;
                userInfo.value.avatarUrl = userInfo.value.headimgurl;
            } else {
                userInfo.value.displayName = userInfo.value.name;
                userInfo.value.avatarUrl = userInfo.value.picture;
            }
            
            status.value = "Success!";
            return; 
        }

        // Standard OAuth / Drogon Flow
        // Validate state
        const savedState = localStorage.getItem('auth_state_drogon');
        if (state !== savedState) {
            throw new Error("Invalid state parameter (CSRF Protection)");
        }
        localStorage.removeItem('auth_state_drogon');

        const tokenUrl = '/oauth2/token';
        const tokenBody = {
            grant_type: 'authorization_code',
            code: code,
            client_id: 'vue-client',
            client_secret: '123456',
            redirect_uri: window.location.origin + '/callback'
        };

        // Exchange Code
        const tokenResponse = await fetch(tokenUrl, {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: new URLSearchParams(tokenBody)
        });

        if (!tokenResponse.ok) {
            throw new Error(`Token exchange failed: ${tokenResponse.status}`);
        }

        const tokenData = await tokenResponse.json();
        const accessToken = tokenData.access_token;
        
        status.value = "Fetching user info...";

        // Fetch User Info
        const userResponse = await fetch('/oauth2/userinfo', {
            headers: { 'Authorization': `Bearer ${accessToken}` }
        });
        
        if (!userResponse.ok) {
            throw new Error(`User Info failed: ${userResponse.status}`);
        }

        userInfo.value = await userResponse.json();
        
        if (userInfo.value.name) {
            userInfo.value.displayName = userInfo.value.name;
        }

        status.value = "Success!";

    } catch (e) {
        error.value = e.message;
        status.value = "Error";
    }
})
</script>

<template>
  <div class="callback-container">
    <div class="glass-card">
        <div class="logo">🚀</div>
        
        <!-- Loading State -->
        <div v-if="!userInfo && !error" class="status-box">
            <div class="spinner"></div>
            <p>{{ status }}</p>
        </div>

        <!-- Error State -->
        <div v-if="error" class="error-box">
            <div class="icon">❌</div>
            <h3>Authentication Failed</h3>
            <p>{{ error }}</p>
            <router-link to="/" class="btn-primary">Return Home</router-link>
        </div>
        
        <!-- Success State -->
        <div v-if="userInfo" class="success-box">
            <div class="icon">🎉</div>
            <h3>Login Successful!</h3>
            <p class="welcome-text">Welcome back, <strong>{{ userInfo.displayName }}</strong></p>
            
            <div class="user-profile">
                <img v-if="userInfo.avatarUrl" :src="userInfo.avatarUrl" alt="Avatar" class="avatar">
                <div v-else class="avatar-placeholder">{{ userInfo.displayName ? userInfo.displayName[0].toUpperCase() : 'U' }}</div>
                
                <div class="user-details">
                    <pre>{{ JSON.stringify(userInfo, null, 2) }}</pre>
                </div>
            </div>

            <router-link to="/" class="btn-primary">Continue to Dashboard</router-link>
        </div>
    </div>
  </div>
</template>

<style scoped>
:root {
  --primary-color: #667eea;
  --bg-primary: #0f0f23;
  --glass-bg: rgba(255, 255, 255, 0.05);
  --glass-border: rgba(255, 255, 255, 0.1);
  --text-primary: #e2e8f0;
  --text-secondary: #a0aec0;
}

.callback-container {
    min-height: 100vh;
    display: flex;
    align-items: center;
    justify-content: center;
    background: #0f0f23;
    background: radial-gradient(circle at 50% 0%, #1a1a2e 0%, #0f0f23 100%);
    color: #e2e8f0;
    font-family: 'Inter', sans-serif;
}

.glass-card {
    background: rgba(255, 255, 255, 0.05);
    backdrop-filter: blur(20px);
    border: 1px solid rgba(255, 255, 255, 0.1);
    border-radius: 20px;
    padding: 3rem;
    width: 100%;
    max-width: 500px;
    box-shadow: 0 20px 50px rgba(0, 0, 0, 0.5);
    text-align: center;
    animation: fadeIn 0.5s ease-out;
}

.logo {
    font-size: 3rem;
    margin-bottom: 1.5rem;
    animation: bounce 2s infinite;
}

.status-box, .error-box, .success-box {
    display: flex;
    flex-direction: column;
    align-items: center;
    gap: 1rem;
}

.spinner {
    width: 40px;
    height: 40px;
    border: 3px solid rgba(255, 255, 255, 0.1);
    border-top-color: #667eea;
    border-radius: 50%;
    animation: spin 1s linear infinite;
}

.btn-primary {
    display: inline-block;
    margin-top: 1.5rem;
    padding: 0.8rem 2rem;
    background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
    color: white;
    text-decoration: none;
    border-radius: 50px;
    font-weight: 600;
    transition: transform 0.2s, box-shadow 0.2s;
}

.btn-primary:hover {
    transform: translateY(-2px);
    box-shadow: 0 10px 20px rgba(118, 75, 162, 0.3);
}

.user-profile {
    margin-top: 1.5rem;
    width: 100%;
}

.avatar {
    width: 80px;
    height: 80px;
    border-radius: 50%;
    border: 3px solid #667eea;
    margin-bottom: 1rem;
}

.avatar-placeholder {
    width: 80px;
    height: 80px;
    border-radius: 50%;
    background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
    display: flex;
    align-items: center;
    justify-content: center;
    font-size: 2rem;
    font-weight: bold;
    margin: 0 auto 1rem;
}

pre {
    background: rgba(0, 0, 0, 0.3);
    padding: 1rem;
    border-radius: 8px;
    text-align: left;
    overflow-x: auto;
    font-size: 0.85rem;
    color: #a0aec0;
    max-height: 200px;
    width: 100%;
}

@keyframes spin { to { transform: rotate(360deg); } }
@keyframes fadeIn { from { opacity: 0; transform: translateY(20px); } to { opacity: 1; transform: translateY(0); } }
@keyframes bounce { 0%, 100% { transform: translateY(0); } 50% { transform: translateY(-10px); } }
</style>
