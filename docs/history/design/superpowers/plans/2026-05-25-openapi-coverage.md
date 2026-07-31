# OpenAPI Complete Coverage Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Complete the OpenAPI documentation by adding `OpenApiGenerator::addEndpoint` calls to all remaining Drogon controllers in the system.

**Architecture:** Each controller will be updated to include an `initApiDocs()` static initialization block (using an anonymous namespace struct or similar pattern as done in `OAuth2Controller`), which constructs `EndpointInfo` objects and registers them via `OpenApiGenerator::addEndpoint()`. The `openapi.json` file will then be re-generated and converted to `openapi.yaml`.

**Tech Stack:** C++, Drogon, OpenAPI 3.0

---

### Task 1: Document AdminApiController Endpoints

**Files:**
- Modify: `OAuth2Server/controllers/AdminApiController.cc`

- [ ] **Step 1: Add API Docs Initialization**

Add the necessary `#include <oauth2/OpenApiGenerator.h>` at the top.
Create an anonymous namespace with a struct `AdminApiControllerDocs` to initialize endpoints on load.

```cpp
#include <oauth2/OpenApiGenerator.h>

// ... other includes

namespace {
struct AdminApiControllerDocs {
    AdminApiControllerDocs() {
        // TODO: Document all endpoints defined in AdminApiController.h
        // /api/admin/clients (GET, POST)
        // /api/admin/clients/{clientId} (GET, PUT, DELETE)
        // /api/admin/clients/{clientId}/reset-secret (POST)
        // /api/admin/clients/{clientId}/scopes (GET, PUT)
        // /api/admin/users (GET)
        // /api/admin/users/{userId}/disable (PUT)
        // /api/admin/users/{userId}/roles (PUT)
        // /api/admin/scopes (GET)
        // /api/admin/logs (GET)
        // /api/admin/tokens (GET)
        // /api/admin/tokens/revoke-by-client (POST)
        // /api/admin/tokens/revoke-by-user (POST)
        // /api/admin/tokens/{tokenPrefix} (DELETE)
        // /api/admin/oidc/keys (GET)
        
        // Example for one:
        /*
        common::documentation::EndpointInfo listClients;
        listClients.path = "/api/admin/clients";
        listClients.method = "GET";
        listClients.summary = "List OAuth2 Clients";
        listClients.description = "Get a paginated list of registered OAuth2 clients.";
        listClients.tags = {"Admin", "Clients"};
        listClients.requiresAuth = true;
        // add query parameters like page, size
        common::documentation::OpenApiGenerator::addEndpoint(listClients);
        */
        // Implement full documentation for all these endpoints. Keep examples simple but accurate.
    }
};
AdminApiControllerDocs docs_;
} // namespace
```

- [ ] **Step 2: Commit the changes**

```bash
git add OAuth2Server/controllers/AdminApiController.cc
git commit -m "docs(openapi): add AdminApiController endpoints"
```

### Task 2: Document User Self-Service & Account Endpoints

**Files:**
- Modify: `OAuth2Server/controllers/UserSelfServiceController.cc`
- Modify: `OAuth2Server/controllers/EmailVerificationController.cc`
- Modify: `OAuth2Server/controllers/PasswordResetController.cc`

- [ ] **Step 1: Update UserSelfServiceController.cc**

Add the `initApiDocs` pattern to document `/api/me` (GET, DELETE), `/api/me/password` (PUT), `/api/me/authorized-apps` (GET), and `/api/me/authorized-apps/{clientId}` (DELETE). 
Assign them to the "User Profile" tag.

- [ ] **Step 2: Update EmailVerificationController.cc**

Add the `initApiDocs` pattern to document `/api/verify-email` (GET) and `/api/verify-email/resend` (POST). 
Assign them to the "User Verification" tag.

- [ ] **Step 3: Update PasswordResetController.cc**

Add the `initApiDocs` pattern to document `/api/password-reset/request` (POST) and `/api/password-reset/confirm` (POST). 
Assign them to the "User Verification" tag.

- [ ] **Step 4: Commit the changes**

```bash
git add OAuth2Server/controllers/UserSelfServiceController.cc OAuth2Server/controllers/EmailVerificationController.cc OAuth2Server/controllers/PasswordResetController.cc
git commit -m "docs(openapi): add user self-service and verification endpoints"
```

### Task 3: Document MFA, Auth & Client Endpoints

**Files:**
- Modify: `OAuth2Server/controllers/MfaController.cc`
- Modify: `OAuth2Server/controllers/DeviceAuthController.cc`
- Modify: `OAuth2Server/controllers/WebAuthnController.cc`
- Modify: `OAuth2Server/controllers/ClientRegistrationController.cc`
- Modify: `OAuth2Server/controllers/OrganizationController.cc`

- [ ] **Step 1: Document MfaController.cc**

Document `/oauth2/mfa/setup` (POST), `/oauth2/mfa/setup/verify` (POST), `/oauth2/mfa/disable` (POST), `/oauth2/mfa/verify` (POST). Tags: "MFA".

- [ ] **Step 2: Document DeviceAuthController.cc**

Document `/oauth2/device_authorization` (POST), `/oauth2/device/verify` (GET, POST). Tags: "OAuth2", "Device Flow".

- [ ] **Step 3: Document WebAuthnController.cc**

Document `/oauth2/webauthn/register/begin` (POST), `/oauth2/webauthn/register/finish` (POST), `/oauth2/webauthn/login/begin` (POST), `/oauth2/webauthn/login/finish` (POST), `/oauth2/webauthn/credentials` (GET). Tags: "WebAuthn".

- [ ] **Step 4: Document ClientRegistrationController.cc**

Document `/oauth2/register` (POST). Tags: "OAuth2", "Dynamic Registration".

- [ ] **Step 5: Document OrganizationController.cc**

Document `/api/orgs` (POST, GET), `/api/orgs/{orgId}/users` (POST). Tags: "Organization".

- [ ] **Step 6: Commit the changes**

```bash
git add OAuth2Server/controllers/MfaController.cc OAuth2Server/controllers/DeviceAuthController.cc OAuth2Server/controllers/WebAuthnController.cc OAuth2Server/controllers/ClientRegistrationController.cc OAuth2Server/controllers/OrganizationController.cc
git commit -m "docs(openapi): add mfa, device flow, webauthn, and org endpoints"
```

### Task 4: Regenerate OpenAPI JSON and YAML

- [ ] **Step 1: Rebuild and Restart Server**

```bash
.\scripts\backend\build.bat -debug
taskkill /F /IM OAuth2Server.exe 2>$null
.\scripts\backend\run_server.bat -debug
```
*(Wait 2 seconds for server to start)*

- [ ] **Step 2: Update YAML File**

```bash
Invoke-RestMethod -Uri "http://localhost:5555/docs/api/openapi.json" | ConvertTo-Json -Depth 10 > openapi_new.json
python -c "import json, yaml; data = json.load(open('openapi_new.json', encoding='utf-16')); yaml.safe_dump(data, open('OAuth2Server/openapi.yaml', 'w', encoding='utf-8'), default_flow_style=False, sort_keys=False)"
Remove-Item openapi_new.json
```

- [ ] **Step 3: Commit the changes**

```bash
git add OAuth2Server/openapi.yaml
git commit -m "docs(openapi): regenerate openapi.yaml with all endpoints"
```
