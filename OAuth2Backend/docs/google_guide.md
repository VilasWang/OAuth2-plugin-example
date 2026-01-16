# Google Login Integration Guide

This document provides a step-by-step guide to configuring "Login with Google" for this project.

## 1. Prerequisites

To implement real Google login, you need:
1.  **Google Cloud Project**: Register at [Google Cloud Console](https://console.cloud.google.com/).
2.  **OAuth 2.0 Client ID**: Create an "OAuth client ID" of type "Web application".
3.  **Authorized Redirect URI**: Add `http://localhost:5173/callback` to the "Authorized redirect URIs".
4.  **Client ID and Client Secret**: You will get these after creating the OAuth client.

---

## 2. Backend Configuration

You need to provide the server with your `Client ID` and `Client Secret` so it can exchange the authorization code for an access token.

**File**: `OAuth2Backend/controllers/GoogleController.cc`

Open this file and find the following lines at the top:

```cpp
// TODO: REPLACE WITH YOUR REAL GOOGLE CREDENTIALS
const std::string GOOGLE_CLIENT_ID = "YOUR_GOOGLE_CLIENT_ID";
const std::string GOOGLE_CLIENT_SECRET = "YOUR_GOOGLE_CLIENT_SECRET";
```

1.  Replace `YOUR_GOOGLE_CLIENT_ID` with your **Client ID**.
2.  Replace `YOUR_GOOGLE_CLIENT_SECRET` with your **Client Secret**.
3.  **Rebuild the Backend**:
    ```powershell
    cd OAuth2Backend
    build.bat
    ```

---

## 3. Frontend Configuration

The frontend initiates the login by redirecting the user to Google's OAuth2 authorization page.

**File**: `OAuth2Frontend/src/views/Login.vue`

Find the `loginWithGoogle` function:

```javascript
const loginWithGoogle = () => {
    localStorage.setItem('auth_provider', 'google');
    
    // TODO: REPLACE WITH YOUR GOOGLE CLIENT ID
    const CLIENT_ID = "YOUR_GOOGLE_CLIENT_ID"; 
    // ...
}
```

1.  Replace `YOUR_GOOGLE_CLIENT_ID` with your **Client ID**.
2.  Ensure the `REDIRECT_URI` matches what you configured in the Google Cloud Console.

---

## 4. Verification

1.  Start Backend and Frontend.
2.  Open `http://localhost:5173`.
3.  Click **"Login with Google"**.
4.  Select your Google account and authorize.
5.  You will be redirected back and see your Google profile (Name, Email, Picture).

---

## 5. Note on Security

In a production environment, you should never hardcode `Client Secrets` in your source code. Use environment variables or a secure configuration manager.
