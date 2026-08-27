# Social Login Guide

Backend social login is implemented with a "provider adapter" pattern; **GitHub is currently the only provider fully wired end to end**, while Google and WeChat follow a "backend-ready, frontend wires itself" model (backend routes and configuration are in place; the frontend buttons must be wired in separately).

## GitHub (fully wired, mainline)

- Backend route: `POST /api/github/login`; the `frontends/user` frontend already has a "Sign in with GitHub" button
  (the OAuth App's client id is injected via `VITE_GITHUB_CLIENT_ID`).
- Account model: `oauth2_subject_mappings(provider, subject)` maps to a local user; on first login, a local account with
  the default `user` role is created per the `createLinkedUser` semantics (username collisions are rejected — fail-closed).

## Google (backend ready)

1. Create an OAuth 2.0 client on the Google Cloud side (Web application; set the callback to `https://<your-host>/api/google/login`).
2. Backend configuration (`config.json`):

```json
"external_auth": {
    "google": {
        "client_id": "<your-client-id>",
        "client_secret": "<your-client-secret>"
    }
}
```

3. The backend route `POST /api/google/login` accepts `{ code, redirect_uri }` and completes the code exchange.
4. **The frontend button must be wired in yourself** (the current UI has no built-in Google button — use curl to call the backend route directly when verifying).

## WeChat (backend ready; requires a public callback domain)

1. Create a website application on the WeChat Open Platform (**localhost callbacks are not supported**; an ICP-registered domain is required).
2. Backend configuration follows the same structure (`external_auth.wechat`: appid / app_secret).
3. Backend route: `POST /api/wechat/login`.
4. Three tricks for local development: point the callback domain to 127.0.0.1 via the hosts file / an Nginx reverse proxy / an intranet tunnel.

## General Security Notes

- The `state` on social callbacks must be verified (CSRF protection); the subject returned by a provider is trusted only
  from the server-side code exchange result — never trust user information submitted by the frontend.
- Unlinking a social account has "last login method" protection and a known limitation around concurrent-unlink races
  (see the social-link design archived under `docs/history` and the CHANGELOG #54/#69 fix records).

> Merged from the retired google-guide.md and wechat-guide.md (docs governance A2), and fixed the self-contradiction
> in the Google guide where "there was no frontend button, yet users were told to click the button".
