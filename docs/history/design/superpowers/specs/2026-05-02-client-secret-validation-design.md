# Client Secret Validation Design

**Date:** 2026-05-02
**Author:** OAuth2 Plugin Development Team
**Status:** Design Approved

## Problem Statement

Current OAuth2 token endpoint has a critical security vulnerability: `client_secret` parameter is read but not validated during token exchange. This allows unauthorized access to confidential client resources, violating OAuth2 RFC 6749 security requirements.

**Security Impact:** Confidential clients can be impersonated if authorization code is intercepted, as the system doesn't verify the client's secret.

## Solution Overview

Implement proper client authentication according to OAuth2 RFC 6749 by adding `client_secret` validation for confidential clients while maintaining backward compatibility with public clients.

## Architecture

### Core Components

**1. Type System Enhancement**
- Create `common/types/OAuth2Types.h` with centralized type definitions
- Add `ClientType` enum (PUBLIC, CONFIDENTIAL)
- Add `GrantType` enum for type safety
- Add HTTP status code mapping function

**2. Data Model Extension**
- Add `clientType` field to `OAuth2Client` structure
- Update SQL schema to include client type column
- Migration strategy: DROP and recreate (acceptable - not in production)

**3. Client Classification**
- **vue-client**: Single-page application → Classified as **PUBLIC**
- **Future clients**: Explicitly specify type during registration
- **Default fallback**: CONFIDENTIAL (safer for security)

**4. Enhanced Validation Flow**
```
Token Request → Controller (extract credentials)
              ↓
         Check HTTP Basic Auth OR body parameters
              ↓
         Plugin → validateClient(type-aware logic)
              ↓
         Storage → verify secret (constant-time comparison)
              ↓
         exchangeCodeForToken() with validated client
```

**5. HTTP Basic Authentication Support**
- Parse `Authorization: Basic base64(client_id:client_secret)` header
- Decode and validate credentials
- Fallback to body parameters for backward compatibility

## Implementation Details

### Type System

**File:** `common/types/OAuth2Types.h`

```cpp
namespace oauth2 {
enum class ClientType {
    PUBLIC,
    CONFIDENTIAL
};

enum class GrantType {
    AUTHORIZATION_CODE,
    REFRESH_TOKEN,
    CLIENT_CREDENTIALS,
    IMPLICIT
};

enum class OAuth2Error {
    INVALID_CLIENT,
    INVALID_GRANT,
    UNAUTHORIZED_CLIENT,
    UNSUPPORTED_GRANT_TYPE
};

// Helper functions
std::string clientTypeToString(ClientType type);
ClientType stringToClientType(const std::string& str);

// HTTP status code mapping
int getHttpStatusCode(OAuth2Error error);
}
```

### HTTP Basic Authentication Support

**Controller Layer Implementation:**
```cpp
// In OAuth2Controller::token()
std::string clientId, clientSecret;

// Try HTTP Basic Authentication first
std::string authHeader = req->getHeader("Authorization");
if (!authHeader.empty() && authHeader.substr(0, 6) == "Basic ") {
    // Decode Basic Auth: base64(client_id:client_secret)
    std::string decoded = drogon::utils::base64Decode(authHeader.substr(6));
    size_t colonPos = decoded.find(':');
    if (colonPos != std::string::npos) {
        clientId = decoded.substr(0, colonPos);
        clientSecret = decoded.substr(colonPos + 1);
    }
}

// Fallback to body parameters if Basic Auth not provided
if (clientId.empty()) {
    clientId = req->getParameter("client_id");
    clientSecret = req->getParameter("client_secret");
}
```

### HTTP Status Code Mapping

**Implementation in OAuth2Types.h:**
```cpp
int getHttpStatusCode(OAuth2Error error) {
    switch (error) {
        case OAuth2Error::INVALID_CLIENT:
            return 401;  // Unauthorized
        case OAuth2Error::UNAUTHORIZED_CLIENT:
            return 401;  // Unauthorized
        case OAuth2Error::INVALID_GRANT:
        case OAuth2Error::UNSUPPORTED_GRANT_TYPE:
        default:
            return 400;  // Bad Request
    }
}
```

### PKCE Support (Future Enhancement)

**Data Model Extension:**
```cpp
struct OAuth2AuthCode {
    std::string code;
    std::string clientId;
    std::string redirectUri;
    std::string userId;
    std::string scope;
    std::string codeChallenge;       // PKCE: hash of code_verifier
    std::string codeChallengeMethod; // PKCE: "plain" or "S256"
    int64_t expiresAt;
};
```

**Token Endpoint PKCE Validation:**
```cpp
// In token endpoint, after code validation
if (!authCode.codeChallenge.empty()) {
    std::string codeVerifier = req->getParameter("code_verifier");
    if (codeVerifier.empty()) {
        return error("invalid_request", "code_verifier required");
    }

    std::string computedChallenge;
    if (authCode.codeChallengeMethod == "S256") {
        // SHA256(code_verifier) -> base64url
        computedChallenge = sha256_base64url(codeVerifier);
    } else {
        computedChallenge = codeVerifier; // "plain" method
    }

    if (computedChallenge != authCode.codeChallenge) {
        return error("invalid_grant", "Invalid code_verifier");
    }
}
```

### Data Model Changes

**Updated OAuth2Client Structure:**
```cpp
struct OAuth2Client {
    std::string clientId;
    ClientType clientType;  // New field
    std::string clientSecretHash;
    std::string salt;
    std::vector<std::string> redirectUris;
    std::vector<std::string> allowedScopes;
};
```

### Storage Layer Validation

**Enhanced validateClient() Logic:**

- Check client type first
- For CONFIDENTIAL clients: verify secret hash
- For PUBLIC clients: accept without secret validation
- Log warnings for inconsistent requests

### Plugin Layer Changes

**Updated exchangeCodeForToken Signature:**
```cpp
void exchangeCodeForToken(
    const std::string &code,
    const std::string &clientId,
    const std::string &clientSecret,  // New parameter
    std::function<void(const Json::Value &)> &&callback
);
```

### Controller Layer Integration

**Token Endpoint Validation Flow:**
```cpp
// 1. Extract parameters
std::string clientId, clientSecret, grantType, code;

// 2. Validate client first
plugin->validateClient(clientId, clientSecret, [this, code, clientId, grantType, callback](bool isValid) {
    if (!isValid) {
        return error("invalid_client", "Client authentication failed");
    }
    
    // 3. Proceed with token exchange
    plugin->exchangeCodeForToken(code, clientId, clientSecret, callback);
});
```

## Error Handling

### OAuth2 Compliant Error Responses

**invalid_client (400 Bad Request):**
```json
{
  "error": "invalid_client",
  "error_description": "Client authentication failed"
}
```

### Edge Cases

**1. Missing client_secret for confidential client:**
- Reject with `invalid_client` error
- Log security event

**2. Public client provides secret:**
- Accept but log warning (compatibility)
- Ignore secret in validation

**3. Missing clientType in database:**
- Default to CONFIDENTIAL (safer)
- Log warning for data consistency

## Security Considerations

**Logging Security:**
- Never log secret values
- Log validation failures for monitoring
- Implement rate limiting for failed attempts (already configured in production)

**Timing Attack Protection (P0 - CRITICAL):**
- Use OpenSSL's `CRYPTO_memcmp()` for constant-time comparison
- Available via project's existing OpenSSL 1.1.1t dependency
- Implementation: `#include <openssl/crypto.h>`
- Avoid early returns and branching based on secret comparison

**HTTP Basic Authentication Support (P0 - CRITICAL):**
- Implement RFC 6749 Section 2.3.1 compliance
- Support `Authorization: Basic base64(client_id:client_secret)` header
- Parse HTTP Basic Auth in Controller layer
- Fallback to body parameters for compatibility

**Error Response HTTP Status Codes (P0 - CRITICAL):**
- `invalid_client` → 401 Unauthorized (not 400)
- `invalid_grant` → 400 Bad Request
- `unauthorized_client` → 401 Unauthorized
- Other errors → 400 Bad Request

**Secret Hashing (P1 - STATUS QUO):**
- Current implementation: SHA-256 + salt (adequate for current use)
- No third-party library introduction per user requirement
- Drogon project has OpenSSL 1.1.1t dependency available
- Future enhancement: Consider Argon2 when stricter requirements emerge

**PKCE for Public Clients (P1 - RECOMMENDED):**
- Implement RFC 7636 Proof Key for Code Exchange
- Required for modern public client security
- Mitigates authorization code interception attacks
- Implementation: Add `code_challenge` and `code_verifier` parameters

## Testing Strategy

### Unit Tests (DROGON_TEST)

**Type System Tests:**
- ClientType string conversion
- Enum validation
- HTTP status code mapping

**Client Validation Tests:**
- Confidential client with valid secret → Success
- Confidential client with invalid secret → Failure
- Public client without secret → Success
- Public client with secret → Success (with warning)

**Security Tests (P0):**

- Timing attack resistance: Verify constant-time comparison
- Secret validation never leaks timing information
- HTTP Basic Authentication parsing and validation
- Error response HTTP status codes (401 vs 400)

### Integration Tests

**Token Endpoint Tests:**
- Full authorization code flow with confidential client
- Full authorization code flow with public client
- HTTP Basic Authentication token request
- Error response validation (all error types)
- Backward compatibility verification

**Security Integration Tests:**

- Brute force protection (rate limiting verification)
- Secret exposure in logs (verify no secrets logged)
- HTTP Basic Auth with invalid credentials
- Missing client_secret for confidential client

## Implementation Steps

### P0 - Critical Security Fixes (Required for Production)

1. Create type system in `common/types/OAuth2Types.h`
   - ClientType enum (PUBLIC, CONFIDENTIAL)
   - GrantType enum for type safety
   - OAuth2Error enum for error handling
   - HTTP status code mapping function

2. Update data model in `IOAuth2Storage.h` and SQL schema
   - Add clientType field to OAuth2Client structure
   - DROP and recreate oauth2_clients table
   - Migration: Manual DROP + psql script execution (acceptable - not in production)

3. Enhance storage validation in `validateClient()` implementations
   - Add type-aware client authentication logic
   - Implement constant-time secret comparison using OpenSSL CRYPTO_memcmp()
   - Add HTTP status code mapping for OAuth2 errors

4. Modify plugin interface - Update `exchangeCodeForToken()` signature
   - Add clientSecret parameter
   - Update all storage implementations
   - Update controller calls

5. Integrate controller validation in token endpoint
   - Implement HTTP Basic Authentication parsing
   - Add client validation before token exchange
   - Return correct HTTP status codes (401 for auth failures)

6. Write comprehensive tests with DROGON_TEST
   - Unit tests for type system and validation logic
   - Security tests for timing attack resistance
   - Integration tests for token endpoint

7. Verify backward compatibility with existing clients
   - Test vue-client (PUBLIC) without secret
   - Verify public clients work without secret
   - Test HTTP Basic Authentication

### P1 - Recommended Enhancements (Future Work)

8. Implement PKCE support per RFC 7636
   - Extend OAuth2AuthCode data model
   - Update authorization endpoint to generate challenges
   - Update token endpoint to verify code_verifier

9. Enhanced monitoring and security logging
   - Log failed authentication attempts
   - Implement alerting for suspicious patterns

## Impact Analysis

**Modified Files:**
- `common/types/OAuth2Types.h` (new)
- `storage/IOAuth2Storage.h`
- `storage/PostgresOAuth2Storage.cc`
- `storage/MemoryOAuth2Storage.cc`
- `plugins/OAuth2Plugin.h/.cc`
- `controllers/OAuth2Controller.cc`
- `test/ClientSecretValidationTest.cc` (new)
- `sql/001_oauth2_core.sql`

**Backward Compatibility:**
- ✅ Public clients (vue-client) continue working
- ✅ Existing confidential clients need proper secret
- ⚠️  Database regeneration required

## Success Criteria

- ✅ Confidential clients must provide valid client_secret
- ✅ Public clients work without client_secret
- ✅ OAuth2 compliant error responses
- ✅ All tests pass (unit + integration)
- ✅ No regression in existing functionality
- ✅ Security audit confirms vulnerability fixed

## Future Enhancements

- Add explicit client registration endpoint
- Implement client credentials grant type
- Add PKCE support for public clients
- Implement client rotation/rekeying
rekeying
