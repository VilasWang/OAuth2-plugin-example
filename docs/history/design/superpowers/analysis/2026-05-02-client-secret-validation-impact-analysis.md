# Client Secret Validation Impact Analysis

**Date:** 2026-05-02
**Scope:** Full system impact analysis for implementing client_secret validation
**Key Decision:** vue-client classified as PUBLIC Client (no secret required)

## Executive Summary

**Total Files to Modify:** 11 core files
**Lines of Code Change:** ~400 lines
**Breaking Changes:** Yes (plugin interface signature change)
**Testing Required:** Unit + Integration + Browser E2E

## Detailed Impact Analysis

### 1. Data Model Layer (3 files)

#### 1.1 OAuth2Backend/common/types/OAuth2Types.h (NEW FILE)
**Impact:** Create new type system
**Changes:**
- Add `namespace oauth2`
- Add `enum class ClientType { PUBLIC, CONFIDENTIAL }`
- Add `enum class GrantType { ... }`
- Add `enum class OAuth2Error { ... }`
- Add helper functions: `clientTypeToString()`, `stringToClientType()`
- Add `getHttpStatusCode(OAuth2Error)` function

**Risk:** Low (new file, no existing code affected)

---

#### 1.2 OAuth2Backend/storage/IOAuth2Storage.h
**Impact:** Update OAuth2Client structure and validateClient signature
**Current Code:**
```cpp
struct OAuth2Client {
    std::string clientId;
    std::string clientSecretHash;
    std::string salt;
    std::vector<std::string> redirectUris;
    std::vector<std::string> allowedScopes;
};
```

**Required Changes:**
```cpp
struct OAuth2Client {
    std::string clientId;
    ClientType clientType;  // NEW FIELD
    std::string clientSecretHash;
    std::string salt;
    std::vector<std::string> redirectUris;
    std::vector<std::string> allowedScopes;
};
```

**validateClient Signature Change:**
```cpp
// BEFORE:
virtual void validateClient(const std::string &clientId,
                           const std::string &clientSecret,
                           BoolCallback &&cb) = 0;

// AFTER: (Signature unchanged, but implementation must be type-aware)
// Implementation must check clientType before validating secret
```

**Risk:** Medium (interface change affects all implementations)

**Affected Implementations:**
- PostgresOAuth2Storage.cc
- MemoryOAuth2Storage.cc
- RedisOAuth2Storage.cc
- CachedOAuth2Storage.cc

---

#### 1.3 OAuth2Backend/sql/001_oauth2_core.sql
**Impact:** Update schema to include client_type column
**Current Schema:**
```sql
CREATE TABLE oauth2_clients (
    client_id VARCHAR(50) PRIMARY KEY,
    client_secret VARCHAR(100) NOT NULL,
    salt VARCHAR(50) NOT NULL,
    ...
);
```

**Required Changes:**
```sql
CREATE TABLE oauth2_clients (
    client_id VARCHAR(50) PRIMARY KEY,
    client_type VARCHAR(20) NOT NULL DEFAULT 'CONFIDENTIAL',  -- NEW COLUMN
    client_secret VARCHAR(100) NOT NULL,
    salt VARCHAR(50) NOT NULL,
    ...
);
```

**Sample Data Update:**
```sql
-- BEFORE:
INSERT INTO oauth2_clients (client_id, client_secret, salt, ...)
VALUES ('vue-client', '42a121b6...', 'random_salt', ...);

-- AFTER:
INSERT INTO oauth2_clients (client_id, client_type, client_secret, salt, ...)
VALUES ('vue-client', 'PUBLIC', '42a121b6...', 'random_salt', ...);
```

**Migration Strategy:**
- DROP TABLE + Recreate (acceptable - not in production)
- vue-client seeded as PUBLIC

**Risk:** Low (database not in production)

---

### 2. Storage Layer Implementation (4 files)

#### 2.1 OAuth2Backend/storage/PostgresOAuth2Storage.cc
**Impact:** Critical security fixes + type-aware validation
**Current Issues:**
1. Timing-vulnerable secret comparison (line 154-174)
2. No clientType field in ORM mapping
3. validateClient() not type-aware

**Required Changes:**

**A. Update ORM Mapping:**
```cpp
// Add clientType to field mapping
Mapper<Oauth2Clients> mapper(clientClient_);
mapper.findOne(
    Criteria(Oauth2Clients::Cols::_client_id, CompareOperator::EQ, clientId),
    [this, cb](const Oauth2Clients &ormClient) {
        oauth2::OAuth2Client client;
        client.clientId = ormClient.getValueOfClientId();
        client.clientType = stringToClientType(ormClient.getValueOfClientType());  // NEW
        // ... rest of mapping
    }
);
```

**B. Fix Timing Attack Vulnerability (CRITICAL):**
```cpp
// BEFORE (VULNERABLE):
bool match = true;
for (size_t i = 0; i < computedHash.length(); ++i) {
    if (std::tolower(computedHash[i]) != std::tolower(storedHash[i])) {
        match = false;
        break;  // EARLY RETURN LEAKS TIMING INFO
    }
}

// AFTER (SECURE):
#include <openssl/crypto.h>

bool match = (CRYPTO_memcmp(computedHash.c_str(), storedHash.c_str(),
                            std::min(computedHash.length(), storedHash.length())) == 0)
             && computedHash.length() == storedHash.length();
```

**C. Type-Aware Validation Logic:**
```cpp
void PostgresOAuth2Storage::validateClient(
    const std::string &clientId,
    const std::string &clientSecret,
    BoolCallback &&cb) {

    getClient(clientId, [this, clientSecret, cb](std::optional<OAuth2Client> optClient) {
        if (!optClient) {
            (*cb)(false);
            return;
        }

        auto client = *optClient;

        // PUBLIC clients skip secret validation
        if (client.clientType == ClientType::PUBLIC) {
            LOG_DEBUG << "PUBLIC client " << client.clientId << " accepted without secret";
            (*cb)(true);
            return;
        }

        // CONFIDENTIAL clients MUST validate secret
        if (clientSecret.empty()) {
            LOG_WARN << "CONFIDENTIAL client " << client.clientId << " missing secret";
            (*cb)(false);
            return;
        }

        // Constant-time secret comparison
        bool match = verifySecretConstantTime(clientSecret, client.clientSecretHash, client.salt);
        (*cb)(match);
    });
}
```

**Risk:** High (core security logic)

---

#### 2.2 OAuth2Backend/storage/MemoryOAuth2Storage.cc
**Impact:** Same security fixes as PostgreSQL
**Required Changes:**
- Add clientType field to in-memory client structure
- Implement same type-aware validation logic
- Use same constant-time comparison function

**Risk:** Medium (test environment only)

---

#### 2.3 OAuth2Backend/storage/RedisOAuth2Storage.cc
**Impact:** Update Redis storage to support clientType
**Required Changes:**
- Add clientType field to Redis hash structure
- Update serialization/deserialization logic
- Implement type-aware validation

**Risk:** Low (cached storage, derives from interface)

---

#### 2.4 OAuth2Backend/storage/CachedOAuth2Storage.cc
**Impact:** Update caching layer
**Required Changes:**
- Ensure clientType is included in cache key
- Update cache invalidation logic if needed

**Risk:** Low (wrapper layer)

---

### 3. Plugin Layer (2 files)

#### 3.1 OAuth2Backend/plugins/OAuth2Plugin.h
**Impact:** Update exchangeCodeForToken signature
**Current Signature:**
```cpp
void exchangeCodeForToken(
    const std::string &code,
    const std::string &clientId,
    std::function<void(const Json::Value &)> &&callback);
```

**Required Signature:**
```cpp
void exchangeCodeForToken(
    const std::string &code,
    const std::string &clientId,
    const std::string &clientSecret,  // NEW PARAMETER
    std::function<void(const Json::Value &)> &&callback);
```

**Risk:** High (breaking change to all callers)

**Affected Callers:**
- OAuth2Controller.cc (token endpoint)

---

#### 3.2 OAuth2Backend/plugins/OAuth2Plugin.cc
**Impact:** Update implementation to pass clientSecret
**Required Changes:**
- Update exchangeCodeForToken implementation signature
- Pass clientSecret to storage layer for validation
- Validate client BEFORE consuming auth code

**Risk:** High (core OAuth2 logic)

---

### 4. Controller Layer (1 file)

#### 4.1 OAuth2Backend/controllers/OAuth2Controller.cc
**Impact:** Implement HTTP Basic Auth + client validation
**Current Issues:**
1. Reads clientSecret but never validates it (line 442)
2. No HTTP Basic Authentication support
3. Wrong HTTP status codes (400 instead of 401)
4. No client validation before token exchange

**Required Changes:**

**A. Add HTTP Basic Authentication Parsing:**
```cpp
// In OAuth2Controller::token()
std::string clientId, clientSecret;

// Try HTTP Basic Authentication first
std::string authHeader = req->getHeader("Authorization");
if (!authHeader.empty() && authHeader.substr(0, 6) == "Basic ") {
    std::string decoded = drogon::utils::base64Decode(authHeader.substr(6));
    size_t colonPos = decoded.find(':');    if (colonPos != std::string::npos) {
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

**B. Add Client Validation BEFORE Token Exchange:**
```cpp
// BEFORE (line 459):
plugin->exchangeCodeForToken(code, clientId, callback);

// AFTER:
plugin->validateClient(clientId, clientSecret, [this, code, clientId, callback](bool isValid) {
    if (!isValid) {
        Json::Value error;
        error["error"] = "invalid_client";
        error["error_description"] = "Client authentication failed";
        callback->setResponseStatus(k401Unauthorized);  // 401 not 400
        callback->setBodyJson(error);
        return;
    }

    // Proceed with token exchange
    plugin->exchangeCodeForToken(code, clientId, clientSecret, callback);
});
```

**C. Use Framework's Base64 Decode Utility:**
```cpp
// Use drogon::utils::base64Decode() instead of custom implementation
// to reduce code redundancy and potential vulnerabilities.
```

**D. Update Error Response HTTP Status Codes:**
```cpp
int getHttpStatusCode(const std::string &oauth2Error) {
    if (oauth2Error == "invalid_client" || oauth2Error == "unauthorized_client") {
        return k401Unauthorized;  // 401
    }
    return k400BadRequest;  // 400
}
```

**Risk:** High (core security logic, complex changes)

---

### 5. Frontend Layer (1 file)

#### 5.1 OAuth2Frontend/src/views/Callback.vue
**Impact:** Remove client_secret from token request
**Current Code (SECURITY VIOLATION):**
```javascript
const tokenBody = {
    grant_type: 'authorization_code',
    code: code,
    client_id: 'vue-client',
    client_secret: '123456',  // ❌ SECURITY VIOLATION
    redirect_uri: window.location.origin + '/callback'
};
```

**Required Fix:**
```javascript
const tokenBody = {
    grant_type: 'authorization_code',
    code: code,
    client_id: 'vue-client',
    // client_secret: '123456',  // REMOVED - vue-client is PUBLIC
    redirect_uri: window.location.origin + '/callback'
};
```

**Justification:**
- vue-client is now PUBLIC Client
- PUBLIC Clients do not use client_secret
- Backend will validate clientType and skip secret check

**Risk:** Low (simple removal, backend handles PUBLIC clients)

---

### 6. Testing Layer (1 new file)

#### 6.1 OAuth2Backend/test/ClientSecretValidationTest.cc (NEW FILE)
**Impact:** Comprehensive test suite for client secret validation
**Required Test Coverage:**

**Unit Tests:**
- ClientType string conversion (PUBLIC ↔ "PUBLIC")
- OAuth2Error to HTTP status code mapping
- Constant-time comparison function

**Client Validation Tests:**
- CONFIDENTIAL client with valid secret → Success
- CONFIDENTIAL client with invalid secret → Failure
- CONFIDENTIAL client with missing secret → Failure
- PUBLIC client without secret → Success
- PUBLIC client with secret → Success (warning logged)

**Security Tests:**
- Timing attack resistance (measure comparison time variance)
- Secret not exposed in logs (verify log output)
- HTTP Basic Authentication parsing
- Error response HTTP status codes (401 vs 400)

**Integration Tests:**
- Full OAuth2 flow with CONFIDENTIAL client
- Full OAuth2 flow with PUBLIC client
- HTTP Basic Auth token request
- Backward compatibility

**Risk:** Low (new file, no existing code affected)

---

### 7. Documentation Layer (2 files)

#### 7.1 OAuth2Backend/config.json
**Impact:** Update client configuration documentation
**Required Changes:**
```json
{
  "oauth2": {
    "clients": [
      {
        "client_id": "vue-client",
        "client_type": "PUBLIC",
        "description": "Vue.js frontend application (PUBLIC client - no secret required)",
        "redirect_uris": ["http://localhost:5173/callback"]
      }
    ]
  }
}
```

**Risk:** Low (documentation only)

---

#### 7.2 OAuth2Backend/docs/api_reference.md
**Impact:** Update token endpoint documentation
**Required Changes:**
- Document HTTP Basic Authentication support
- Document PUBLIC vs CONFIDENTIAL client behavior
- Update error response examples with correct HTTP status codes
- Add examples for both authentication methods

**Risk:** Low (documentation only)

---

## Risk Matrix

| File | Risk Level | Complexity | Breaking Change | Priority |
|------|-----------|-----------|-----------------|----------|
| OAuth2Types.h (new) | Low | Low | No | P0 |
| IOAuth2Storage.h | Medium | Medium | Yes | P0 |
| 001_oauth2_core.sql | Low | Low | No | P0 |
| PostgresOAuth2Storage.cc | High | High | No | P0 |
| MemoryOAuth2Storage.cc | Medium | Medium | No | P0 |
| RedisOAuth2Storage.cc | Low | Low | No | P1 |
| CachedOAuth2Storage.cc | Low | Low | No | P1 |
| OAuth2Plugin.h | High | Medium | Yes | P0 |
| OAuth2Plugin.cc | High | High | No | P0 |
| OAuth2Controller.cc | High | High | No | P0 |
| Callback.vue | Low | Low | No | P0 |
| ClientSecretValidationTest.cc (new) | Low | Medium | No | P0 |
| config.json | Low | Low | No | P1 |
| api_reference.md | Low | Low | No | P1 |

---

## Dependency Graph

```
OAuth2Types.h (NEW)
    ↓
IOAuth2Storage.h
    ↓
    ├─→ PostgresOAuth2Storage.cc
    ├─→ MemoryOAuth2Storage.cc
    ├─→ RedisOAuth2Storage.cc
    └─→ CachedOAuth2Storage.cc
            ↓
    OAuth2Plugin.h
        ↓
    OAuth2Plugin.cc
        ↓
    OAuth2Controller.cc
        ↓
    Callback.vue (Frontend)
```

**Implementation Order:** Bottom-up (from dependencies to consumers)

---

## Rollback Strategy

**If critical issues arise:**

1. **Code Rollback:**
   ```bash
   git revert <commit-range>
   ```

2. **Database Rollback:**
   ```sql
   DROP TABLE oauth2_clients;
   -- Recreate from backup schema
   ```

3. **Frontend Rollback:**
   - Restore client_secret: '123456' in Callback.vue

**Rollback Time:** < 5 minutes

---

## Success Criteria

### Functional
- [ ] vue-client (PUBLIC) works without client_secret
- [ ] CONFIDENTIAL clients require valid client_secret
- [ ] HTTP Basic Authentication supported
- [ ] Correct HTTP status codes (401 for auth failures)

### Security
- [ ] Constant-time comparison prevents timing attacks
- [ ] No secrets in logs
- [ ] Client validation BEFORE token exchange

### Quality
- [ ] All tests pass (Unit + Integration)
- [ ] Test coverage > 80%
- [ ] Browser E2E test successful
- [ ] No regression in existing functionality

---

## Estimated Effort

- **Phase 1 (Type System):** 2 hours
- **Phase 2 (Storage Layer):** 4 hours
- **Phase 3 (Plugin/Controller):** 6 hours
- **Phase 4 (Testing):** 4 hours
- **Phase 5 (Documentation):** 2 hours

**Total Estimated Effort:** 18 hours

---

## Next Steps

1. ✅ Impact analysis complete
2. ⏭️ Begin Phase 1: Create OAuth2Types.h
3. ⏭️ Execute implementation plan sequentially
4. ⏭️ Run tests after each phase
5. ⏭️ Browser E2E test after Phase 4
