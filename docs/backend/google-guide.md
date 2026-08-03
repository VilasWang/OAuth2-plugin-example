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

You need to provide the server with your `Client ID` and your `Client Secret` so it can exchange the authorization code for an access token.

**Config**: `apps/server/config/config.json` → `plugins[OAuth2Plugin].config.external_auth.google`

The `GoogleController` (`libs/drogon/src/controllers/GoogleController.cc`) reads these values from the plugin config at runtime, under `external_auth.google`:

```json
{
    "external_auth": {
        "google": {
            "client_id": "YOUR_GOOGLE_CLIENT_ID",
            "client_secret": "YOUR_GOOGLE_CLIENT_SECRET"
        }
    }
}
```

1.  Replace `YOUR_GOOGLE_CLIENT_ID` with your **Client ID**.
2.  Replace `YOUR_GOOGLE_CLIENT_SECRET` with your **Client Secret**.
3.  **Rebuild the Backend** (run from the repo root):
    ```powershell
    .\manage.ps1 build-backend
    ```

---

## 3. Frontend Configuration

The current user frontend (`frontends/user/`) does not ship a "Login with Google" button — only the backend `/api/google/login` endpoint is implemented (and GitHub is the social login wired into `frontends/user/src/pages/auth/LoginPage.vue` via `VITE_GITHUB_CLIENT_ID`). To add Google login to the SPA, follow the same Vite build-time env pattern (e.g. a `VITE_GOOGLE_CLIENT_ID` consumed in `LoginPage.vue`) and call the backend `/api/google/login` endpoint with the authorization `code`.

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
