# oasdiff breaking-change ignore list (bootstrap errata)

Used by `.github/workflows/openapi-governance.yml` via `oasdiff breaking
--err-ignore <this file>`. Each line below exempts one breaking change from
the gate; oasdiff matches "METHOD /path" plus the change description text.
Every entry must carry a reason — this file is the audit trail of every
breaking change we consciously allowed through the gate.

If you are adding an entry for a NEW change: prefer a major version bump
instead. This list is not a rubber stamp; reviewers should challenge entries.

---

## 2026-08-26 · #71 social-link server-side state (PR: issues batch 1)

The link POST gains a REQUIRED `state` property. The endpoint was introduced
in PR #68 (three weeks old), is consumed only by this repo's own SPA, and both
SDKs are regenerated in the same PR that adds the field -- there are no
external callers to break. A stateless POST is precisely the
provider-code-injection surface this change closes; a soft (optional-state)
transition would keep the vulnerability open for one release.

- POST /api/me/social/links/{provider} added the new required request property `state`

---

## 2026-08-16 · spec-governance M0 bootstrap (PR: openapi spec governance)

One-time reconciliation between the spec and the code (design
docs/productization-evolution/todo/client-sdk-facility-design.md §10.1).

### Dead-endpoint removals (8) — documented endpoints that had NO backing route

The pre-M0 YAML documented endpoints that were removed from the code long
ago; keeping them was the bug, removing them is the fix. No client can be
calling these successfully.

- GET /api/orgs api path removed without deprecation — route removed; orgs live under /api/admin/organizations
- POST /api/orgs api path removed without deprecation — same
- POST /api/orgs/{orgId}/users api path removed without deprecation — same
- GET /oauth2/device/verify api path removed without deprecation — verification UI is frontend-rendered; no route ever existed
- POST /oauth2/device/verify api path removed without deprecation — same
- POST /oauth2/mfa/disable api path removed without deprecation — real route is POST /api/me/mfa/disable
- POST /oauth2/mfa/setup api path removed without deprecation — real route is POST /api/me/mfa/setup
- POST /oauth2/mfa/setup/verify api path removed without deprecation — real route is POST /api/me/mfa/verify

### Request-parameter relocation to form/JSON bodies (6) — the wire contract did NOT change

These endpoints always read parameters via Drogon getParameter/getJsonObject
(query OR form OR JSON body). The old spec wrongly declared them as query
parameters; the new spec declares the RFC-correct form/JSON body. The server
accepts both, so existing callers keep working.

- POST /oauth2/token added required request body — form-encoded body per RFC 6749 §4.1.3; query params still accepted by the server
- POST /oauth2/introspect added required request body — form-encoded per RFC 7662 §2.1; query still accepted
- POST /oauth2/revoke added required request body — form-encoded per RFC 7009 §2.1; query still accepted
- POST /oauth2/login added required request body — JSON or form body; the server still reads query parameters too
- POST /oauth2/mfa/verify added required request body — JSON or form body; query still accepted
- POST /oauth2/device_authorization added required request body — form-encoded per RFC 8628 §3.1; query still accepted

### Response body correction (1) — the old spec lied

- POST /oauth2/revoke removed the media type `application/json` for the response with the status `200` — the 200 body is EMPTY per RFC 7009 (the old spec declared a JSON object that never existed)

## 2026-08-30 · IAM hardening tranche 1: /api/register password minLength (PR: hardening tranche 1)

`in API POST /api/register for the \`query\` request parameter \`password\`, the
minLength was increased from \`0\` to \`8\``

Intentional security tightening (#103): the server now enforces a minimum
password length (auth.min_password_length, default 8) that previously accepted
any non-empty password — NIST SP 800-63B length floor. The only caller is this
repo's own user SPA (updated in the same PR); the SDKs are regenerated here.
The old behavior accepted 1-character passwords; nothing external can depend
on keeping that hole open. Other register params unaffected.
