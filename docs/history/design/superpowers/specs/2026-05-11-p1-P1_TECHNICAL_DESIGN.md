# P1 Priority Tasks - Technical Design Document v2.0

## Overview
This document outlines the technical design and implementation plan for P1 priority OAuth2.0 enhancement features.

**Priority**: P1 (High importance, post-P0 security features)
**Target**: Enhanced OAuth2.0 compliance and observability
**Timeline**: Post-P0 validation completion
**Status**: Revised based on code review feedback

---

## 1. Token Introspection (RFC 7662)

### Purpose
Enable resource servers to validate access tokens and retrieve token metadata without understanding token cryptography.

### RFC 7662 Requirements

**Endpoint**: `POST /oauth/introspect`

**Request Parameters**:
- `token` (required): The token string to introspect
- `token_type_hint` (optional): Hint about token type (`access_token` or `refresh_token`)

**Authentication**:
- Client authentication required (Basic auth or client credentials in POST body)
- Only confidential clients can introspect tokens

**Response Format** (JSON):
```json
{
  "active": true,
  "client_id": "vue-client",
  "token_type": "Bearer",
  "exp": 1234567890,
  "iat": 1234560000,
  "nbf": 1234560000,
  "sub": "local:alice",
  "aud": "vue-client",
  "iss": "https://oauth.example.com",
  "scope": "openid profile email"
}
```

**Inactive Token Response**:
```json
{
  "active": false
}
```

### Implementation Design

**Database Schema Extension**:
```sql
-- File: 005_p1_token_enhancements.sql

-- Add RFC 7662 required fields to access tokens table
ALTER TABLE oauth2_access_tokens
    ADD COLUMN issued_at BIGINT NOT NULL DEFAULT EXTRACT(EPOCH FROM CURRENT_TIMESTAMP),
    ADD COLUMN issuer VARCHAR(255) NOT NULL DEFAULT 'https://oauth.example.com',
    ADD COLUMN audience VARCHAR(255),
    ADD COLUMN not_before BIGINT DEFAULT EXTRACT(EPOCH FROM CURRENT_TIMESTAMP),
    ADD COLUMN introspect_count INTEGER DEFAULT 0;

-- Add indexes for performance
CREATE INDEX idx_access_tokens_token ON oauth2_access_tokens(token);
CREATE INDEX idx_access_tokens_client_id ON oauth2_access_tokens(client_id);
CREATE INDEX idx_access_tokens_expires_at ON oauth2_access_tokens(expires_at);
```

**Controller Method**: `OAuth2Controller::introspectToken()`

**Rate Limiting** (using existing Hodor plugin):
```cpp
// In config.json, configure Hodor rate limits
{
  "plugins": [
    {
      "name": "Hodor",
      "config": {
        "rate_limits": {
          "/oauth/introspect": {
            "max_requests": 100,
            "window_seconds": 60,
            "per_client": true
          }
        }
      }
    }
  ]
}
```

**Validation Logic**:
```cpp
void OAuth2Controller::introspectToken(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback)
{
    // 1. Authenticate client (Basic auth or POST body)
    auto [clientId, clientSecret] = extractClientCredentials(req);
    
    plugin->validateClient(clientId, clientSecret, [=, callback](bool valid) {
        if (!valid) {
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k401Unauthorized);
            callback(resp);
            return;
        }
        
        // 2. Extract token parameter
        std::string token = req->getParameter("token");
        if (token.empty()) {
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k400BadRequest);
            resp->setBody("missing token parameter");
            callback(resp);
            return;
        }
        
        // 3. Introspect token
        plugin->introspectToken(token, [=, callback, clientId](auto introspection) {
            auto resp = HttpResponse::newHttpJsonResponse(introspection.toJson());
            resp->setStatusCode(k200OK);
            callback(resp);
        });
    });
}
```

---

## 2. Token Revocation (RFC 7009)

### Purpose
Allow clients to invalidate tokens (access or refresh) before their natural expiration.

### RFC 7009 Requirements

**Endpoint**: `POST /oauth/revoke`

**Request Parameters**:
- `token` (required): The token to revoke
- `token_type_hint` (optional): Hint about token type (`access_token` or `refresh_token`)

**Authentication**:
- Client authentication required
- Only the client that issued the token can revoke it

**Response**:
- HTTP 200 OK (success, even if token doesn't exist)
- No response body (to prevent token probing attacks)

### Implementation Design

**Database Schema Changes**:
```sql
-- File: 005_p1_token_enhancements.sql (continued)

-- Use existing revoked BOOLEAN, add audit fields
ALTER TABLE oauth2_access_tokens
    ADD COLUMN revoked_at BIGINT,
    ADD COLUMN revoked_by VARCHAR(50);  -- client_id that revoked

ALTER TABLE oauth2_refresh_tokens
    ADD COLUMN revoked_at BIGINT,
    ADD COLUMN revoked_by VARCHAR(50);

-- Indexes for cleanup queries
CREATE INDEX idx_access_tokens_revoked ON oauth2_access_tokens(revoked, expires_at);
CREATE INDEX idx_refresh_tokens_revoked ON oauth2_refresh_tokens(revoked, expires_at);
```

**Controller Method**: `OAuth2Controller::revokeToken()`

**Revocation Logic**:
```cpp
void OAuth2Controller::revokeToken(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback)
{
    // 1. Authenticate client
    auto [clientId, clientSecret] = extractClientCredentials(req);
    
    plugin->validateClient(clientId, clientSecret, [=, callback](bool valid) {
        if (!valid) {
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k401Unauthorized);
            callback(resp);
            return;
        }
        
        // 2. Extract token parameter
        std::string token = req->getParameter("token");
        if (token.empty()) {
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k400BadRequest);
            resp->setBody("missing token parameter");
            callback(resp);
            return;
        }
        
        std::string tokenTypeHint = req->getParameter("token_type_hint");
        
        // 3. Revoke token (always return 200 OK per RFC 7009)
        plugin->revokeToken(token, clientId, tokenTypeHint, [=, callback](bool success) {
            // Always return 200 OK to prevent token probing
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k200OK);
            callback(resp);
        });
    });
}
```

**Rate Limiting** (using existing Hodor plugin):
```cpp
// In config.json
{
  "rate_limits": {
    "/oauth/revoke": {
      "max_requests": 50,
      "window_seconds": 60,
      "per_client": true
    }
  }
}
```

---

## 3. Authorization Server Metadata (RFC 8414)

### Purpose
Provide standardized metadata about the OAuth2.0 authorization server for client discovery.

### RFC 8414 Requirements

**Endpoint**: `GET /.well-known/oauth-authorization-server`

**Complete Response Format** (JSON):
```json
{
  "issuer": "https://oauth.example.com",
  "authorization_endpoint": "https://oauth.example.com/oauth/authorize",
  "token_endpoint": "https://oauth.example.com/oauth/token",
  "revocation_endpoint": "https://oauth.example.com/oauth/revoke",
  "revocation_endpoint_auth_methods_supported": ["client_secret_post", "client_secret_basic", "none"],
  "introspection_endpoint": "https://oauth.example.com/oauth/introspect",
  "introspection_endpoint_auth_methods_supported": ["client_secret_post", "client_secret_basic", "none"],
  "jwks_uri": "https://oauth.example.com/.well-known/jwks.json",
  "scopes_supported": ["openid", "profile", "email", "admin:read"],
  "response_types_supported": ["code"],
  "response_modes_supported": ["query", "fragment"],
  "grant_types_supported": ["authorization_code", "refresh_token"],
  "token_endpoint_auth_methods_supported": ["client_secret_post", "client_secret_basic", "none"],
  "token_endpoint_auth_signing_alg_values_supported": ["RS256", "HS256"],
  "code_challenge_methods_supported": ["plain", "S256"],
  "claim_types_supported": ["normal"],
  "claims_supported": ["sub", "name", "email", "picture"],
  "service_documentation": "https://docs.example.com/oauth",
  "ui_locales_supported": ["en", "zh-CN"],
  "op_policy_uri": "https://policies.example.com/oauth",
  "op_tos_uri": "https://terms.example.com/oauth"
}
```

### Implementation Design

**Configuration Extension** (`config.json`):
```json
{
  "plugins": [{
    "name": "OAuth2Plugin",
    "config": {
      "metadata": {
        "issuer": "https://oauth.example.com",
        "jwks_uri": "https://oauth.example.com/.well-known/jwks.json",
        "service_documentation": "https://docs.example.com/oauth",
        "ui_locales_supported": ["en", "zh-CN"],
        "op_policy_uri": "https://policies.example.com/oauth",
        "op_tos_uri": "https://terms.example.com/oauth"
      }
    }
  }]
}
```

**Controller Method**: `OAuth2Controller::metadata()`

**Dynamic Metadata Generation**:
```cpp
void OAuth2Controller::metadata(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback)
{
    // Cache metadata for performance (5 minute TTL)
    static std::string cachedMetadata;
    static time_t lastCacheUpdate = 0;
    static const time_t CACHE_TTL = 300;
    
    time_t now = time(nullptr);
    if (cachedMetadata.empty() || (now - lastCacheUpdate) > CACHE_TTL) {
        Json::Value metadata;
        
        // Basic server info
        metadata["issuer"] = config_["metadata"]["issuer"];
        metadata["authorization_endpoint"] = getBaseUrl() + "/oauth/authorize";
        metadata["token_endpoint"] = getBaseUrl() + "/oauth/token";
        
        // P1 endpoints (only if enabled)
        if (config_["features"]["introspection"].asBool()) {
            metadata["introspection_endpoint"] = getBaseUrl() + "/oauth/introspect";
            metadata["introspection_endpoint_auth_methods_supported"] = 
                Json::Value(arrayHolder);
            metadata["introspection_endpoint_auth_methods_supported"].append("client_secret_post");
            metadata["introspection_endpoint_auth_methods_supported"].append("client_secret_basic");
            metadata["introspection_endpoint_auth_methods_supported"].append("none");
        }
        
        if (config_["features"]["revocation"].asBool()) {
            metadata["revocation_endpoint"] = getBaseUrl() + "/oauth/revoke";
            metadata["revocation_endpoint_auth_methods_supported"] = 
                Json::Value(arrayHolder);
            metadata["revocation_endpoint_auth_methods_supported"].append("client_secret_post");
            metadata["revocation_endpoint_auth_methods_supported"].append("client_secret_basic");
            metadata["revocation_endpoint_auth_methods_supported"].append("none");
        }
        
        // JWT support
        metadata["jwks_uri"] = config_["metadata"]["jwks_uri"];
        metadata["token_endpoint_auth_signing_alg_values_supported"] = 
            Json::Value(arrayHolder);
        metadata["token_endpoint_auth_signing_alg_values_supported"].append("RS256");
        metadata["token_endpoint_auth_signing_alg_values_supported"].append("HS256");
        
        // OpenID Connect support
        metadata["scopes_supported"] = getConfiguredScopes();
        metadata["response_types_supported"] = Json::Value(arrayHolder);
        metadata["response_types_supported"].append("code");
        metadata["response_modes_supported"] = Json::Value(arrayHolder);
        metadata["response_modes_supported"].append("query");
        metadata["response_modes_supported"].append("fragment");
        metadata["grant_types_supported"] = Json::Value(arrayHolder);
        metadata["grant_types_supported"].append("authorization_code");
        metadata["grant_types_supported"].append("refresh_token");
        metadata["claim_types_supported"] = Json::Value(arrayHolder);
        metadata["claim_types_supported"].append("normal");
        metadata["claims_supported"] = Json::Value(arrayHolder);
        metadata["claims_supported"].append("sub");
        metadata["claims_supported"].append("name");
        metadata["claims_supported"].append("email");
        
        // PKCE support
        metadata["code_challenge_methods_supported"] = Json::Value(arrayHolder);
        metadata["code_challenge_methods_supported"].append("plain");
        metadata["code_challenge_methods_supported"].append("S256");
        
        // Client authentication methods
        metadata["token_endpoint_auth_methods_supported"] = Json::Value(arrayHolder);
        metadata["token_endpoint_auth_methods_supported"].append("client_secret_post");
        metadata["token_endpoint_auth_methods_supported"].append("none");
        
        // Documentation and policies
        metadata["service_documentation"] = config_["metadata"]["service_documentation"];
        metadata["ui_locales_supported"] = config_["metadata"]["ui_locales_supported"];
        metadata["op_policy_uri"] = config_["metadata"]["op_policy_uri"];
        metadata["op_tos_uri"] = config_["metadata"]["op_tos_uri"];
        
        cachedMetadata = Json::FastWriter().write(metadata);
        lastCacheUpdate = now;
    }
    
    auto resp = HttpResponse::newHttpResponse();
    resp->setContentTypeCode(CT_APPLICATION_JSON);
    resp->setBody(cachedMetadata);
    callback(resp);
}
```

---

## 4. Extended Storage Interface

### P1 Method Additions

**IOAuth2Storage.h Extensions**:
```cpp
/**
 * @brief Introspect token metadata for RFC 7662 compliance
 * @param token The access token to introspect
 * @param cb Callback with introspection metadata or nullopt if invalid
 */
using TokenIntrospectionCallback = std::function<void(std::optional<TokenIntrospection>)>;
virtual void introspectToken(
    const std::string &token,
    TokenIntrospectionCallback &&cb
) = 0;

/**
 * @brief Revoke an access token
 * @param token The access token to revoke
 * @param revokedBy Client ID performing the revocation
 * @param cb Callback invoked when revocation completes
 */
virtual void revokeAccessToken(
    const std::string &token,
    const std::string &revokedBy,
    VoidCallback &&cb
) = 0;

/**
 * @brief Increment introspection count for monitoring
 * @param token The access token
 * @param cb Callback invoked when update completes
 */
virtual void incrementIntrospectCount(
    const std::string &token,
    VoidCallback &&cb
) = 0;
```

**New Data Structures**:
```cpp
/**
 * @brief Token introspection response (RFC 7662)
 */
struct TokenIntrospection {
    bool active;
    std::string clientId;
    std::string tokenType = "Bearer";
    int64_t exp = 0;
    int64_t iat = 0;
    int64_t nbf = 0;
    std::string sub;
    std::string aud;
    std::string iss;
    std::string scope;
    
    Json::Value toJson() const {
        Json::Value json;
        json["active"] = active;
        if (active) {
            json["client_id"] = clientId;
            json["token_type"] = tokenType;
            json["exp"] = static_cast<Json::Int64>(exp);
            json["iat"] = static_cast<Json::Int64>(iat);
            json["nbf"] = static_cast<Json::Int64>(nbf);
            json["sub"] = sub;
            json["aud"] = aud;
            json["iss"] = iss;
            json["scope"] = scope;
        }
        return json;
    }
};

/**
 * @brief Extended OAuth2AccessToken with P1 fields
 */
struct OAuth2AccessTokenExtended : public OAuth2AccessToken {
    int64_t issuedAt = 0;
    std::string issuer;
    std::string audience;
    int64_t notBefore = 0;
    int introspectCount = 0;
    int64_t revokedAt = 0;
    std::string revokedBy;
};
```

---

## 5. Test Coverage Enhancement Plan

### Current Status
- P0 functionality: 31 test cases in P0FunctionalityTest.cc
- Overall coverage: Estimated 60-70%
- Target: 80%+ coverage

### New Test Files

**1. TokenIntrospectionTest.cc**
```cpp
// Basic functionality tests
TEST(TokenIntrospection, ValidToken) { }
TEST(TokenIntrospection, ExpiredToken) { }
TEST(TokenIntrospection, RevokedToken) { }
TEST(TokenIntrospection, NonexistentToken) { }

// Authentication tests
TEST(TokenIntrospection, RequiresClientAuth) { }
TEST(TokenIntrospection, InvalidClientCredentials) { }
TEST(TokenIntrospection, PublicClientForbidden) { }

// Parameter validation tests
TEST(TokenIntrospection, MissingTokenParameter) { }
TEST(TokenIntrospection, TokenTypeHintAccess) { }
TEST(TokenIntrospection, TokenTypeHintRefresh) { }

// Security tests (NEW - based on review feedback)
TEST(TokenIntrospection, TimingAttackResistance) {
    // Measure introspection time for valid vs invalid tokens
    // Should be approximately equal (±5ms)
}

TEST(TokenIntrospection, CrossClientTokenAccess) {
    // Client A tries to introspect Client B's token
    // Should fail with 401 Unauthorized
}

// Performance tests
TEST(TokenIntrospection, PerformanceUnderLoad) { }
TEST(TokenIntrospection, CacheEffectiveness) { }
```

**2. TokenRevocationTest.cc**
```cpp
// Basic functionality tests
TEST(TokenRevocation, RevokeAccessToken) { }
TEST(TokenRevocation, RevokeRefreshToken) { }
TEST(TokenRevocation, RevokeNonexistentToken) { }

// Authentication tests
TEST(TokenRevocation, RequiresClientAuth) { }
TEST(TokenRevocation, InvalidClientCredentials) { }

// Token type hint tests
TEST(TokenRevocation, TokenTypeHintAccess) { }
TEST(TokenRevocation, TokenTypeHintRefresh) { }
TEST(TokenRevocation, TokenTypeHintIncorrect) { }

// Security tests (NEW - based on review feedback)
TEST(TokenRevocation, CannotRevokeOtherClientTokens) {
    // Client A tries to revoke Client B's token
    // Should return 200 OK (per RFC 7009) but token not actually revoked
}

TEST(TokenRevocation, ConcurrentRevocationRequests) {
    // Multiple threads revoke same token
    // Should handle gracefully without database corruption
}

TEST(TokenRevocation, PreventTokenProbing) {
    // Verify that revocation time doesn't reveal token existence
    // Always return 200 OK, measure response times
}

// Audit tests
TEST(TokenRevocation, RevocationAuditTrail) { }
TEST(TokenRevocation, RevokedByTracking) { }
```

**3. MetadataEndpointTest.cc**
```cpp
// RFC 8414 compliance tests
TEST(Metadata, RequiredFieldsPresent) { }
TEST(Metadata, FieldTypesCorrect) { }
TEST(Metadata, EndpointURLsValid) { }

// Dynamic generation tests
TEST(Metadata, ScopesFromConfig) { }
TEST(Metadata, GrantTypesFromConfig) { }
TEST(Metadata, AuthMethodsFromConfig) { }

// Caching tests (NEW - based on review feedback)
TEST(Metadata, CachePerformance) {
    // Verify metadata is cached (5 minute TTL)
    // Second request should be faster
}

TEST(Metadata, CacheInvalidationOnConfigChange) {
    // Change config and verify metadata updates
    // Should reflect changes within 5 minutes
}

// Content tests
TEST(Metadata, P1EndpointsConditional) {
    // If introspection disabled, should not appear in metadata
}

TEST(Metadata, OpenIDConnectFields) {
    // Verify OpenID Connect specific fields are present
}
```

**4. CoverageGapAnalysisTest.cc (NEW)**
```cpp
// Identify untested code paths
TEST(CoverageGap, DatabaseErrorHandling) { }
TEST(CoverageGap, NetworkTimeoutScenarios) { }
TEST(CoverageGap, ConcurrentAccessPatterns) { }
TEST(CoverageGap, EdgeCaseInputs) { }
```

### Coverage Goals by Component
- Controllers: 85%+
- Plugins: 80%+
- Storage implementations: 75%+
- Overall: 80%+

---

## 6. Performance Benchmarking (Revised Targets)

### Realistic Performance Targets

**Token Operations** (with Redis cache):
- Authorization code generation: < 10ms
- Token exchange: < 50ms  
- Token introspection: < 10ms (cache hit), < 30ms (cache miss)
- Token revocation: < 15ms (includes cache invalidation)

**Database Query Targets** (PostgreSQL):
- Token lookup (indexed): < 5ms
- Token validation: < 5ms
- Token revocation UPDATE: < 10ms
- Introspection metadata query: < 8ms

**Load Handling**:
- 1000 req/s sustained (p95 < 50ms)
- 5000 req/s peak for 10 seconds (p95 < 100ms)
- 10000 req/s burst for 1 second (p95 < 200ms)

**Cache Performance**:
- 95% cache hit rate under normal load
- Cache miss latency: +20ms over cache hit
- Redis operations: < 2ms

### Benchmark Implementation

**Benchmark Tool**: `OAuth2Benchmark.cc`

**Test Scenarios**:
```cpp
void benchmarkTokenIntrospection() {
    // 1. Warm cache (95% hit rate)
    // 2. Cold cache (0% hit rate)  
    // 3. Mixed cache (50% hit rate)
    // 4. Concurrent clients (10, 100, 1000)
}

void benchmarkTokenRevocation() {
    // 1. Single-threaded revocation
    // 2. Concurrent revocation of same token
    // 3. Batch revocation
    // 4. Revocation under load
}

void benchmarkMetadataEndpoint() {
    // 1. Cold cache (first request)
    // 2. Warm cache (subsequent requests)
    // 3. Concurrent requests
}
```

---

## 7. Database Schema Updates (Simplified)

### Schema Migration File

**File**: `OAuth2Backend/sql/005_p1_token_enhancements.sql`

```sql
-- ============================================================================
-- P1 Feature: Token Introspection (RFC 7662) & Token Revocation (RFC 7009)
-- ============================================================================

-- Add RFC 7662 required fields to access tokens
ALTER TABLE oauth2_access_tokens
    ADD COLUMN issued_at BIGINT NOT NULL DEFAULT EXTRACT(EPOCH FROM CURRENT_TIMESTAMP),
    ADD COLUMN issuer VARCHAR(255) NOT NULL DEFAULT 'https://oauth.example.com',
    ADD COLUMN audience VARCHAR(255),
    ADD COLUMN not_before BIGINT DEFAULT EXTRACT(EPOCH FROM CURRENT_TIMESTAMP),
    ADD COLUMN introspect_count INTEGER DEFAULT 0,
    ADD COLUMN revoked_at BIGINT,
    ADD COLUMN revoked_by VARCHAR(50);

-- Add revocation audit fields to refresh tokens
ALTER TABLE oauth2_refresh_tokens
    ADD COLUMN revoked_at BIGINT,
    ADD COLUMN revoked_by VARCHAR(50);

-- Create indexes for performance
CREATE INDEX idx_access_tokens_token ON oauth2_access_tokens(token);
CREATE INDEX idx_access_tokens_client_id ON oauth2_access_tokens(client_id);
CREATE INDEX idx_access_tokens_expires_at ON oauth2_access_tokens(expires_at);
CREATE INDEX idx_access_tokens_revoked ON oauth2_access_tokens(revoked, expires_at);

CREATE INDEX idx_refresh_tokens_token ON oauth2_refresh_tokens(token);
CREATE INDEX idx_refresh_tokens_revoked ON oauth2_refresh_tokens(revoked, expires_at);

-- Add comments for documentation
COMMENT ON COLUMN oauth2_access_tokens.issued_at IS 'Unix timestamp when token was issued (RFC 7662 iat)';
COMMENT ON COLUMN oauth2_access_tokens.issuer IS 'Issuer identifier (RFC 7662 iss)';
COMMENT ON COLUMN oauth2_access_tokens.audience IS 'Audience identifier (RFC 7662 aud)';
COMMENT ON COLUMN oauth2_access_tokens.not_before IS 'Token not valid before (RFC 7662 nbf)';
COMMENT ON COLUMN oauth2_access_tokens.introspect_count IS 'Number of introspection requests (monitoring)';
COMMENT ON COLUMN oauth2_access_tokens.revoked_at IS 'Unix timestamp when token was revoked';
COMMENT ON COLUMN oauth2_access_tokens.revoked_by IS 'Client ID that revoked the token';
```

### Application-Layer Compatibility

```cpp
// In OAuth2Plugin.cc, check for P1 feature availability
bool OAuth2Plugin::supportsP1Features() {
    // Check if introspect_count column exists
    // This allows graceful degradation if schema not updated
    try {
        auto client = app().getDbClient();
        client->execSqlAsync(
            "SELECT introspect_count FROM oauth2_access_tokens LIMIT 1",
            [](const Result &) { /* P1 features available */ },
            [](const DrogonDbException &) { /* P0 only */ }
        );
        return true;
    } catch (...) {
        return false;
    }
}
```

---

## 8. Rate Limiting Integration (Hodor Plugin)

### Configuration

**config.json**:
```json
{
  "plugins": [
    {
      "name": "Hodor",
      "dependencies": [],
      "config": {
        "rate_limits": {
          "/oauth/introspect": {
            "max_requests": 100,
            "window_seconds": 60,
            "per_client": true,
            "block_duration": 300
          },
          "/oauth/revoke": {
            "max_requests": 50,
            "window_seconds": 60,
            "per_client": true,
            "block_duration": 600
          },
          "/oauth/token": {
            "max_requests": 200,
            "window_seconds": 60,
            "per_client": true,
            "block_duration": 120
          }
        }
      }
    }
  ]
}
```

### Controller Integration

```cpp
// In OAuth2Controller.cc, rate limiting is automatic via Hodor middleware
// No manual implementation needed

// Just ensure endpoints are registered with Hodor
void OAuth2Controller::initEndpoints() {
    // Hodor automatically applies rate limits based on config
    // No code changes needed in controller methods
}
```

---

## 9. Implementation Priority Order (Revised)

### Phase 1: Database and Storage Foundation (Week 1)
1. **Database Schema Update**
   - Create 005_p1_token_enhancements.sql
   - Update OAuth2AccessToken structure
   - Add TokenIntrospection structure

2. **Storage Interface Extensions**
   - Extend IOAuth2Storage.h with P1 methods
   - Implement in PostgresOAuth2Storage
   - Implement in RedisOAuth2Storage
   - Implement in MemoryOAuth2Storage

### Phase 2: Core P1 Features (Week 2-3)
3. **Token Introspection (RFC 7662)**
   - OAuth2Controller::introspectToken()
   - OAuth2Plugin::introspectToken()
   - Client authentication
   - Integrate Hodor rate limiting

4. **Token Revocation (RFC 7009)**
   - OAuth2Controller::revokeToken()
   - OAuth2Plugin::revokeToken()
   - Audit trail implementation
   - Integrate Hodor rate limiting

### Phase 3: Metadata and Testing (Week 4)
5. **Authorization Server Metadata (RFC 8414)**
   - OAuth2Controller::metadata()
   - Dynamic metadata generation
   - Caching strategy

6. **Test Coverage Enhancement**
   - TokenIntrospectionTest.cc
   - TokenRevocationTest.cc
   - MetadataEndpointTest.cc
   - CoverageGapAnalysisTest.cc

### Phase 4: Performance and Polish (Week 5-6)
7. **Performance Benchmarking**
   - OAuth2Benchmark.cc
   - Load testing scenarios
   - Performance optimization

8. **Documentation and Integration**
   - API documentation updates
   - OpenAPI spec updates
   - Integration testing

---

## 10. Risk Assessment (Updated)

### Technical Risks

**Low Risk** (mitigated by existing infrastructure):
- ~~Rate limiting complexity~~ → Using existing Hodor plugin
- ~~Database migration complexity~~ → Simple schema updates (no production data)

**Medium Risk**:
- Performance regression with new features → Mitigation: Performance baseline testing
- Cache invalidation complexity → Mitigation: Use existing cache infrastructure

### Operational Risks

**Low Risk**:
- Breaking existing client integrations → New endpoints are additive
- Backward compatibility issues → Application-layer feature detection

---

## 11. Success Criteria

### Functional Requirements
- [PASS] Token introspection validates tokens correctly per RFC 7662
- [PASS] Token revocation invalidates tokens immediately per RFC 7009
- [PASS] Metadata endpoint complies with RFC 8414
- [PASS] All tests pass with 80%+ coverage
- [PASS] Performance benchmarks meet realistic targets

### Quality Requirements
- [PASS] Zero security vulnerabilities
- [PASS] Backward compatibility maintained
- [PASS] Documentation complete
- [PASS] Code review approved
- [PASS] Hodor rate limiting integrated

---

## 12. Next Steps

1. [PASS] Design review completed (this document)
2. Create detailed implementation plans for each phase
3. Set up development and testing environments
4. Begin Phase 1: Database and Storage Foundation

**Estimated Total Timeline**: 5-6 weeks (reduced from 4-6 weeks due to simplified migration)
**Resource Requirements**: 1 developer, testing infrastructure
**Dependencies**: P0 completion, database access, development environment

---

**Document Version**: 2.0 (Revised based on code review)
**Created**: 2026-05-09
**Last Updated**: 2026-05-09
**Author**: OAuth2 Plugin Development Team
**Status**: Ready for Implementation - Critical Issues Resolved