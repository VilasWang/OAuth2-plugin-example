<script setup>
import { ref } from 'vue'

const loginWithWeChat = () => {
    localStorage.setItem('auth_provider', 'wechat');
    
    // Generate random state for CSRF protection
    const state = crypto.randomUUID();
    localStorage.setItem('auth_state_wechat', state);
    
    // TODO: VALIDATE CONFIGURATION
    const APPID = "YOUR_WECHAT_APPID"; 
    const REDIRECT_URI = encodeURIComponent("http://your-domain.com/callback"); // Localhost usually blocked by WeChat
    
    const url = `https://open.weixin.qq.com/connect/qrconnect?appid=${APPID}&redirect_uri=${REDIRECT_URI}&response_type=code&scope=snsapi_login&state=${state}#wechat_redirect`;
    
    window.location.href = url;
}

const loginWithDrogon = async () => {
    localStorage.setItem('auth_provider', 'drogon');
    
    // Generate random state for CSRF protection
    const state = crypto.randomUUID();
    localStorage.setItem('auth_state_drogon', state);
    
    const clientId = 'vue-client';
    const redirectUri = 'http://localhost:5173/callback';
    const scope = 'openid profile';
    const authUrl = `http://localhost:5555/oauth2/authorize?response_type=code&client_id=${clientId}&redirect_uri=${redirectUri}&scope=${scope}&state=${state}`;

    // Health check with timeout
    try {
        const controller = new AbortController();
        const timeoutId = setTimeout(() => controller.abort(), 5000); // 5s timeout

        // Just check if we can connect. Expecting 404 or 405 is fine, just not timeout/refused.
        // Or if authorize endpoint supports GET, it might return HTML or redirect.
        // Let's try a HEAD request or just fetch the authorize URL and catch error.
        await fetch(`http://localhost:5555/oauth2/authorize`, { 
            method: 'HEAD', 
            signal: controller.signal,
            mode: 'no-cors' // We just want to check connectivity, not read opaque response
        });
        clearTimeout(timeoutId);
        
        // Proceed if reachable
        window.location.href = authUrl;
    } catch (error) {
        if (error.name === 'AbortError') {
            alert('Login timeout: Backend server is unreachable (5s timeout).');
        } else {
            console.error(error);
            alert(`Login failed: Unable to connect to backend (${error.message}).`);
        }
    }
}

const loginWithGoogle = () => {
    localStorage.setItem('auth_provider', 'google');
    
    // Generate random state for CSRF protection
    const state = crypto.randomUUID();
    localStorage.setItem('auth_state_google', state);
    
    // TODO: REPLACE WITH YOUR GOOGLE CLIENT ID
    const CLIENT_ID = "YOUR_GOOGLE_CLIENT_ID"; 
    const REDIRECT_URI = encodeURIComponent("http://localhost:5173/callback");
    const SCOPE = encodeURIComponent("openid email profile");
    
    const url = `https://accounts.google.com/o/oauth2/v2/auth?client_id=${CLIENT_ID}&redirect_uri=${REDIRECT_URI}&response_type=code&scope=${SCOPE}&state=${state}`;
    
    window.location.href = url;
}

</script>

<template>
  <div class="login-container">
    <h1>OAuth2.0 Demo</h1>
    <div class="card">
        <p>Login with your account</p>
        <button @click="loginWithDrogon" class="btn primary">Login with Drogon (Local)</button>
        <button @click="loginWithWeChat" class="btn wechat">Login with WeChat</button>
        <button @click="loginWithGoogle" class="btn google">Login with Google</button>
    </div>
  </div>
</template>

<style scoped>
.login-container {
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    min-height: 80vh;
}
.card {
    background: #f9f9f9;
    padding: 2rem;
    border-radius: 8px;
    box-shadow: 0 2px 8px rgba(0,0,0,0.1);
    display: flex;
    flex-direction: column;
    gap: 1rem;
    min-width: 300px;
}
.btn {
    padding: 0.8rem;
    border: none;
    border-radius: 4px;
    cursor: pointer;
    font-size: 1rem;
    font-weight: 500;
}
.primary { background-color: #42b883; color: white; }
.wechat { background-color: #07c160; color: white; }
.google { background-color: #db4437; color: white; }
</style>
