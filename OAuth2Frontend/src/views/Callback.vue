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
            
            const response = await fetch(`http://localhost:5556${loginEndpoint}`, {
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

        const tokenUrl = 'http://localhost:5556/oauth2/token';
        const tokenBody = {
            grant_type: 'authorization_code',
            code: code,
            client_id: 'vue-client',
            client_secret: 'vue-secret',
            redirect_uri: 'http://localhost:5173/callback'
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
        const userResponse = await fetch('http://localhost:5556/oauth2/userinfo', {
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
    <h1>Login Result</h1>
    <div v-if="error" class="error">
        <h3>Error</h3>
        <p>{{ error }}</p>
        <router-link to="/">Go Back</router-link>
    </div>
    
    <div v-else-if="userInfo" class="success">
        <h3>Welcome, {{ userInfo.displayName }}!</h3>
        <img v-if="userInfo.avatarUrl" :src="userInfo.avatarUrl" alt="Avatar" style="width:50px; height:50px; border-radius:50%; margin-bottom: 20px;">
        <pre>{{ userInfo }}</pre>
        <router-link to="/">Logout</router-link>
    </div>
    
    <div v-else>
        <p>{{ status }}</p>
    </div>
  </div>
</template>

<style scoped>
.callback-container { padding: 2rem; max-width: 600px; margin: 0 auto; }
.error { color: red; background: #fee; padding: 1rem; border-radius: 4px; }
.success { background: #eef; padding: 1rem; border-radius: 4px; }
pre { background: #eee; padding: 1rem; border-radius: 4px; }
</style>
