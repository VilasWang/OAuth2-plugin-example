# Client Secret Validation Implementation Plan

**Date:** 2026-05-02
**Based on:** `docs/superpowers/specs/2026-05-02-client-secret-validation-design.md`
**Status:** Ready for Implementation

## Overview

Implement OAuth2 RFC 6749 compliant client authentication for the token endpoint, addressing the critical security vulnerability where `client_secret` is read but not validated.

## Implementation Phases

### Phase 1: Type System and Data Model (P0 - Critical)

#### Task 1.1: Create Type System
**File:** `OAuth2Backend/common/types/OAuth2Types.h` (new)

**Requirements:**
- Create `oauth2` namespace with type-safe enums
- Implement `ClientType` enum (PUBLIC, CONFIDENTIAL)
- Implement `GrantType` enum (AUTHORIZATION_CODE, REFRESH_TOKEN, CLIENT_CREDENTIALS, IMPLICIT)
- Implement `OAuth2Error` enum (INVALID_CLIENT, INVALID_GRANT, UNAUTHORIZED_CLIENT, UNSUPPORTED_GRANT_TYPE)
- Add `getHttpStatusCode(OAuth2Error)` function for correct status code mapping
- Add string conversion helpers: `clientTypeToString()`, `stringToClientType()`

**Success Criteria:**
- Header compiles without errors
- All enum values are type-safe
- HTTP status code mapping matches RFC requirements (401 for auth failures, 400 for bad requests)

**Testing:**
- Unit tests for enum conversions
- Unit tests for HTTP status code mapping

---

#### Task 1.2: Update Data Model Interface
**File:** `OAuth2Backend/storage/IOAuth2Storage.h`

**Requirements:**
- Add `ClientType clientType` field to `OAuth2Client` structure
- Update `createClient()` signature to accept `ClientType` parameter
- Update `validateClient()` signature to accept `std::string clientSecret`
- Maintain backward compatibility for existing implementations

**Success Criteria:**
- Interface compiles without breaking existing code
- All storage implementations can be updated to new interface

---

#### Task 1.3: Update SQL Schema
**File:** `OAuth2Backend/sql/001_oauth2_core.sql`

**Requirements:**
- Add `client_type VARCHAR(20) NOT NULL DEFAULT 'CONFIDENTIAL'` column to `oauth2_clients` table
- Create manual migration script:
  ```sql
  -- Backup existing data (if any)
  CREATE TABLE oauth2_clients_backup AS SELECT * FROM oauth2_clients;

  -- Drop and recreate table
  DROP TABLE oauth2_clients;

  -- Recreate with new schema
  CREATE TABLE oauth2_clients (
    client_id VARCHAR(255) PRIMARY KEY,
    client_type VARCHAR(20) NOT NULL DEFAULT 'CONFIDENTIAL',
    client_secret_hash VARCHAR(255) NOT NULL,
    salt VARCHAR(255) NOT NULL,
    redirect_uris TEXT[] NOT NULL,
    allowed_scopes TEXT[] NOT NULL
  );

  -- Restore vue-client as PUBLIC (no secret required, existing hash ignored)
  INSERT INTO oauth2_clients (client_id, client_type, client_secret_hash, salt, redirect_uris, allowed_scopes)
  VALUES ('vue-client', 'PUBLIC', '<existing_hash>', '<existing_salt>', ARRAY['http://localhost:5173/callback'], ARRAY['openid', 'profile']);
  ```

**Success Criteria:**
- SQL script executes without errors
- vue-client is properly classified as PUBLIC
- Default value is CONFIDENTIAL (safer for security)

---

### Phase 2: Storage Layer Security Fixes (P0 - Critical)

#### Task 2.1: Update PostgreSQL Storage Implementation
**File:** `OAuth2Backend/storage/PostgresOAuth2Storage.cc`

**Requirements:**
- Update `OAuth2Client` ORM mapping to include `clientType` field
- Modify `createClient()` to handle `ClientType` parameter
- **CRITICAL:** Replace timing-vulnerable secret comparison with constant-time comparison:
  ```cpp
  #include <openssl/crypto.h>

  bool verifySecret(const std::string &inputSecret, const std::string &storedHash, const std::string &salt) {
      std::string computedHash = computeSHA256(inputSecret + salt);
      // Constant-time comparison to prevent timing attacks
      return CRYPTO_memcmp(computedHash.c_str(), storedHash.c_str(),
                          std::min(computedHash.length(), storedHash.length())) == 0
             && computedHash.length() == storedHash.length();
  }
  ```
- Implement type-aware `validateClient()` logic:
  - For CONFIDENTIAL clients: verify secret hash
  - For PUBLIC clients: skip secret validation
  - Log warnings for inconsistent requests
- Map `OAuth2Error` to correct HTTP status codes

**Success Criteria:**
- Secret comparison uses constant-time algorithm
- CONFIDENTIAL clients require valid secret
- PUBLIC clients accepted without secret
- No timing information leaked through early returns

**Testing:**
- Unit test: CONFIDENTIAL client with valid secret → Success
- Unit test: CONFIDENTIAL client with invalid secret → Failure
- Unit test: PUBLIC client without secret → Success
- Security test: Timing attack resistance (measure comparison time)

---

#### Task 2.2: Update Memory Storage Implementation
**File:** `OAuth2Backend/storage/MemoryOAuth2Storage.cc`

**Requirements:**
- Apply same security fixes as PostgreSQL implementation
- Ensure test consistency between storage implementations
- Use same constant-time comparison function

**Success Criteria:**
- Same behavior as PostgreSQL implementation
- Tests pass on Windows (Memory-only tests)

---

### Phase 3: Plugin and Controller Integration (P0 - Critical)

#### Task 3.1: Update Plugin Interface
**Files:** `OAuth2Backend/plugins/OAuth2Plugin.h`, `OAuth2Backend/plugins/OAuth2Plugin.cc`

**Requirements:**
- Update `exchangeCodeForToken()` signature:
  ```cpp
  void exchangeCodeForToken(
      const std::string &code,
      const std::string &clientId,
      const std::string &clientSecret,  // New parameter
      std::function<void(const Json::Value &)> &&callback
  );
  ```
- Update `validateClient()` to accept `clientSecret` parameter
- Pass `clientSecret` to storage layer for validation
- Validate client BEFORE proceeding with token exchange

**Success Criteria:**
- Plugin interface matches new storage interface
- Client validation occurs before token exchange
- Invalid client credentials rejected with proper error

---

#### Task 3.2: Implement HTTP Basic Authentication
**File:** `OAuth2Backend/controllers/OAuth2Controller.cc`

**Requirements:**
- Implement HTTP Basic Authentication parsing in `token()` endpoint:
  ```cpp
  std::string clientId, clientSecret;

  // Try HTTP Basic Authentication first
  std::string authHeader = req->getHeader("Authorization");
  if (!authHeader.empty() && authHeader.substr(0, 6) == "Basic ") {
      std::string decoded = drogon::utils::base64Decode(authHeader.substr(6));
      size_t colonPos = decoded.find(':');
      if (colonPos != std::string::npos) {
          clientId = decoded.substr(0, colonPos);
          clientSecret = decoded.substr(colonPos + 1);
      }
  }

  // Fallback to body parameters
  if (clientId.empty()) {
      clientId = req->getParameter("client_id");
      clientSecret = req->getParameter("client_secret");
  }
  ```
- Use Drogon's utility `drogon::utils::base64Decode` for Base64 decoding.
- Add client validation before token exchange:
  ```cpp
  plugin->validateClient(clientId, clientSecret, [this, code, clientId, grantType, callback](bool isValid) {
      if (!isValid) {
          Json::Value error;
          error["error"] = "invalid_client";
          error["error_description"] = "Client authentication failed";
          callback->setResponseStatus(k401Unauthorized);  // CRITICAL: 401 not 400
          callback->setBodyJson(error);
          return;
      }

      // Proceed with token exchange
      plugin->exchangeCodeForToken(code, clientId, clientSecret, callback);
  });
  ```

**Success Criteria:**
- HTTP Basic Auth credentials correctly parsed and decoded
- Fallback to body parameters works for backward compatibility
- Invalid client credentials return 401 Unauthorized
- Valid credentials proceed to token exchange

**Testing:**
- Integration test: HTTP Basic Auth with valid credentials → Success
- Integration test: HTTP Basic Auth with invalid credentials → 401 error
- Integration test: Body parameters with valid credentials → Success (backward compatibility)

---

#### Task 3.3: Update Error Response Handling
**File:** `OAuth2Backend/controllers/OAuth2Controller.cc`

**Requirements:**
- Implement HTTP status code mapping based on OAuth2 error type:
  ```cpp
  int getHttpStatusCode(const std::string &oauth2Error) {
      if (oauth2Error == "invalid_client" || oauth2Error == "unauthorized_client") {
          return k401Unauthorized;  // 401
      }
      return k400BadRequest;  // 400 for other errors
  }
  ```
- Update all error responses to use correct status codes

**Success Criteria:**
- `invalid_client` → 401 Unauthorized
- `unauthorized_client` → 401 Unauthorized
- `invalid_grant` → 400 Bad Request
- Other errors → 400 Bad Request

---

### Phase 4: Testing and Verification (P0 - Critical)

#### Task 4.1: Create Comprehensive Test Suite
**File:** `OAuth2Backend/test/ClientSecretValidationTest.cc` (new)

**Requirements:**
- **Unit Tests:**
  - `ClientType` string conversion tests
  - `OAuth2Error` to HTTP status code mapping tests
  - Constant-time comparison function tests

- **Client Validation Tests:**
  - CONFIDENTIAL client with valid secret → Success
  - CONFIDENTIAL client with invalid secret → Failure
  - CONFIDENTIAL client with missing secret → Failure
  - PUBLIC client without secret → Success
  - PUBLIC client with secret → Success (with warning logged)

- **Security Tests:**
  - Timing attack resistance: Measure comparison time variance
  - Secret not exposed in logs (verify log output)
  - Brute force protection (rate limiting verification)

- **Integration Tests:**
  - Full authorization code flow with CONFIDENTIAL client
  - Full authorization code flow with PUBLIC client
  - HTTP Basic Authentication token request
  - Error response validation (all error types)
  - Backward compatibility with existing vue-client

**Success Criteria:**
- All tests pass (DROGON_TEST framework)
- Test coverage > 80% for new code
- Security tests confirm timing attack resistance
- Integration tests pass on both Linux (PostgreSQL) and Windows (Memory)

---

#### Task 4.2: Browser Integration Testing
**Requirements:**
- Test full OAuth2 flow using vue-client:
  1. Navigate to Login page
  2. Click "Sign in with Drogon"
  3. Complete authorization
  4. Verify token endpoint is called WITHOUT client_secret (as PUBLIC client)
  5. Verify user info displayed correctly
- Test error cases:
  - Attempting to pass invalid secret to PUBLIC client (should ignore or log warning but succeed)
  - Verify proper error messages displayed if authorization fails

**Success Criteria:**
- vue-client successfully authenticates without client_secret
- No regression in existing functionality

---

### Phase 5: Documentation and Cleanup (P0 - Critical)

#### Task 5.1: Update API Documentation
**File:** `OAuth2Backend/controllers/OAuth2Controller.cc`

**Requirements:**
- Update OpenAPI/Swagger documentation for `/oauth2/token` endpoint:
  - Document HTTP Basic Authentication support
  - Document client_secret parameter requirement
  - Document error responses with correct HTTP status codes
  - Add examples for both Basic Auth and body parameter methods

**Success Criteria:**
- OpenAPI documentation matches implementation
- Examples are accurate and testable

---

#### Task 5.2: Update Configuration Documentation
**File:** `config.json`, `README.md`

**Requirements:**
- Document vue-client as PUBLIC client
- Document client_secret requirement removed for vue-client
- Document migration steps for existing deployments

**Success Criteria:**
- Configuration is clear and reproducible
- Migration steps are documented

---

## Testing Strategy

### Unit Tests (Fast Feedback)
```bash
cd build
ctest -R Unit
```
- Run after each code change
- Expected time: < 30 seconds

### Integration Tests (Linux Only)
```bash
cd build
ctest -R Integration
```
- Run after completing each phase
- Expected time: < 2 minutes
- Tests PostgreSQL integration

### Memory Tests (Windows Compatible)
```bash
cd build
ctest -R Memory
```
- Run on Windows after each phase
- Expected time: < 1 minute

### Browser Tests (Manual)
- Run after completing Phase 4
- Test full OAuth2 flow with vue-client
- Verify error handling

---

## Rollback Strategy

If critical issues arise:
1. Revert to commit before implementation: `git revert <commit-range>`
2. DROP oauth2_clients table and recreate from backup
3. Restore previous configuration

---

## Success Criteria

### Functional Requirements
- [ ] CONFIDENTIAL clients must provide valid client_secret
- [ ] PUBLIC clients work without client_secret
- [ ] HTTP Basic Authentication supported per RFC 6749 Section 2.3.1
- [ ] OAuth2 compliant error responses (401 for auth failures, 400 for bad requests)

### Security Requirements
- [ ] Constant-time secret comparison prevents timing attacks
- [ ] No secrets logged in error messages or debug output
- [ ] Client authentication occurs BEFORE token exchange

### Quality Requirements
- [ ] All unit tests pass (DROGON_TEST)
- [ ] All integration tests pass (Linux PostgreSQL)
- [ ] All memory tests pass (Windows)
- [ ] Test coverage > 80% for new code
- [ ] Browser integration test successful
- [ ] No regression in existing functionality

### Performance Requirements
- [ ] Token endpoint response time < 100ms (excluding database)
- [ ] Constant-time comparison adds < 1ms overhead

---

## Implementation Order

**Execute tasks in sequence:**
1. Task 1.1 → 1.2 → 1.3 (Type system and data model)
2. Task 2.1 → 2.2 (Storage layer security)
3. Task 3.1 → 3.2 → 3.3 (Plugin and controller integration)
4. Task 4.1 → 4.2 (Testing and verification)
5. Task 5.1 → 5.2 (Documentation and cleanup)

**After each phase:**
- Run relevant tests
- Verify no compilation warnings
- Commit changes with descriptive message

**After completing all phases:**
- Run full test suite
- Perform browser integration test
- Create git tag: `v1.1.0-client-secret-validation`

---

## Notes

- **Database Migration:** Since production environment is not live, DROP and recreate is acceptable
- **Secret Hashing:** Current SHA-256 + salt is maintained (no third-party libraries per user requirement)
- **Rate Limiting:** Already configured in production environment
- **Future Work:** PKCE support (P1) documented but not implemented in this phase
