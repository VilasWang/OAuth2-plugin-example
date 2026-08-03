# WeChat Login Integration Guide

This document provides a step-by-step guide to configuring "Login with WeChat" (Website Application / PC Scan Login) for this project.

## 1. Prerequisites

To implement real WeChat login, you need:
1.  **Verified WeChat Open Platform Account**: Register at [open.weixin.qq.com](https://open.weixin.qq.com/).
2.  **Website Application**: Create a "Website App" in the WeChat management console.
3.  **Authorized Domain**: In the app settings, set the "Authorized Callback Domain" (授权回调域).
    *   *Note: WeChat does NOT support IPs or `localhost`.*
    *   *Example Domain*: `passport.yourdomain.com`

---

## 2. Backend Configuration

You need to provide the server with your `AppID` and `AppSecret` so it can exchange the authorization code for an access token.

**Config**: `apps/server/config/config.json` → `plugins[OAuth2Plugin].config.external_auth.wechat`

The `WeChatController` (`libs/drogon/src/controllers/WeChatController.cc`) reads these values from the plugin config at runtime, under `external_auth.wechat`:

```json
{
    "external_auth": {
        "wechat": {
            "appid": "YOUR_WECHAT_APPID",
            "secret": "YOUR_WECHAT_SECRET"
        }
    }
}
```

1.  Replace `YOUR_WECHAT_APPID` with your **AppID**.
2.  Replace `YOUR_WECHAT_SECRET` with your **AppSecret**.
3.  **Rebuild the Backend** (run from the repo root):
    ```powershell
    .\manage.ps1 build-backend
    ```

---

## 3. Frontend Configuration

The current user frontend (`frontends/user/`) does not ship a "Login with WeChat" entry point — only the backend `/api/wechat/login` endpoint is implemented. To add WeChat login to the SPA, follow the same Vite build-time env pattern used for the existing GitHub social login (a `VITE_WECHAT_APPID` consumed in `frontends/user/src/pages/auth/LoginPage.vue`) and redirect the user to WeChat's QR-code authorization page, then call the backend `/api/wechat/login` endpoint with the returned `code`.

When wiring the redirect, note:

1.  Use the **same AppID** configured in the backend (`external_auth.wechat.appid`).
2.  Set the **callback URL** to match the **Authorized Callback Domain** registered in the WeChat Console.
    *   Example: If your authorized domain is `passport.example.com`, your callback URL might be `http://passport.example.com/callback`.

---

## 4. Local Testing Tips (Advanced)

Since WeChat requires a valid domain name, you cannot use `localhost` directly. To test locally:

### Option A: Hosts File Modification
Map a fake domain to your local IP (`127.0.0.1`) in your `C:\Windows\System32\drivers\etc\hosts` file:
```
127.0.0.1   passport.test.com
```
Then access your frontend via `http://passport.test.com:5173`.
*Note: You still need to register `passport.test.com` in WeChat Console, which requires domain ownership verification, so this is tricky for official apps.*

### Option B: Nginx Proxy (Recommended)
Run Nginx locally to proxy requests from a domain to your Vue app and Drogon backend.

### Option C: Intranet Penetration (e.g., Ngrok)
Use a tool like Ngrok to expose your localhost to a public URL that you can register with WeChat.

---

## 5. Troubleshooting

-   **Redirect Fails**: Check if `REDIRECT_URI` matches the domain in WeChat Console exactly.
-   **"Scope Unauthorized"**: Ensure you are using `snsapi_login` scope for PC Websites.
-   **Backend 400 Error**: Check `AppID` and `Secret`. Ensure the server can access the internet (`api.weixin.qq.com`).
