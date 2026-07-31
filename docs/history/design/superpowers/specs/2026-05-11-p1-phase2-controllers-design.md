# P1 Phase 2: Token Introspection & Revocation Controllers - Design Document

**Date**: 2026-05-11
**Phase**: P1 Phase 2
**Status**: Design Approved
**Author**: OAuth2 Plugin Development Team

## Overview

实现 RFC 7662 Token Introspection 和 RFC 7009 Token Revocation 控制器端点，完成 P1 核心功能的应用层实现。

**Objectives**:
- 在现有 OAuth2Controller 中添加两个新端点
- 提供符合 RFC 标准的 token introspection 和 revocation 功能
- 集成现有存储层 P1 方法（Phase 1 已完成）
- 提供完整的单元测试覆盖

**Implementation Approach**: 方案 A - 控制器层直接实现（复用现有架构模式）

---

## 1. Architecture Overview

### 1.1 Request Flow

```
Client Request → OAuth2Controller::introspect/revoke
  → Client Authentication → OAuth2Plugin::validateClient
    → OAuth2Plugin::introspectToken/revokeAccessToken
      → Storage Layer (PostgresOAuth2Storage)
        → Database Query → Return Response
```

### 1.2 Key Components

- **OAuth2Controller**: 新增 `introspect()` 和 `revoke()` 方法
- **OAuth2Plugin**: 已有 P1 方法接口，直接调用
- **Storage Layer**: Phase 1 已实现所有 P1 方法
- **OAuth2Metrics**: 记录 P1 调用指标
- **ErrorHandler**: 统一错误处理

---

## 2. Endpoint Design

### 2.1 Token Introspection Endpoint

**Route Configuration**:
```cpp
ADD_METHOD_TO(OAuth2Controller::introspect, "/oauth2/introspect", Post);
```

**API Specification**:
- **Path**: `POST /oauth2/introspect`
- **Authentication**: HTTP Basic Auth OR POST body `client_id` + `client_secret`
- **Request Parameters**:
  - `token` (required): The token string to introspect
  - `token_type_hint` (optional): Hint about token type
- **Success Response**: `200 OK` + JSON token metadata
- **Error Response**: `401 Unauthorized` (auth failed) or `400 Bad Request` (invalid params)

**Business Logic**:
1. Extract and validate client credentials
2. Validate request parameters (token required)
3. Call `OAuth2Plugin::introspectToken()`
4. Return token introspection data (active=true) or inactive token (active=false)
5. Update monitoring metrics (`introspect_count++`)

### 2.2 Token Revocation Endpoint

**Route Configuration**:
```cpp
ADD_METHOD_TO(OAuth2Controller::revoke, "/oauth2/revoke", Post);
```

**API Specification**:
- **Path**: `POST /oauth2/revoke`
- **Authentication**: HTTP Basic Auth OR POST body `client_id` + `client_secret`
- **Request Parameters**:
  - `token` (required): The token to revoke
  - `token_type_hint` (optional): Hint about token type
- **Response**: `200 OK` (no response body, prevents token probing)
- **Permission Control**: Only the client that issued the token can revoke it

**Business Logic**:
1. Extract and validate client credentials
2. Validate request parameters (token required)
3. Query token to get its owning client_id
4. Verify current client has permission to revoke this token
5. Call `OAuth2Plugin::revokeAccessToken(token, revokedBy=clientId)`
6. Return 200 OK (whether token exists or not)

---

## 3. Client Authentication Implementation

### 3.1 Supported Authentication Methods

**Method 1: HTTP Basic Auth**
```cpp
// Authorization: Basic base64(client_id:client_secret)
auto authHeader = req->getHeader("Authorization");
if (authHeader.find("Basic ") == 0) {
    auto credentials = decodeBasicAuth(authHeader);
    clientId = credentials.first;
    clientSecret = credentials.second;
}
```

**Method 2: POST Body**
```cpp
// application/x-www-form-urlencoded
auto clientId = req->getParameter("client_id");
auto clientSecret = req->getParameter("client_secret");
```

### 3.2 Authentication Logic

```cpp
private:
std::pair<std::string, std::string> OAuth2Controller::extractClientCredentials(
    const HttpRequestPtr &req)
{
    std::string clientId, clientSecret;
    
    // Prefer Basic Auth
    auto authHeader = req->getHeader("Authorization");
    if (!authHeader.empty() && authHeader.find("Basic ") == 0) {
        auto basicAuth = authHeader.substr(6);
        auto decoded = drogon::utils::base64Decode(basicAuth);
        auto colonPos = decoded.find(':');
        if (colonPos != std::string::npos) {
            clientId = decoded.substr(0, colonPos);
            clientSecret = decoded.substr(colonPos + 1);
        }
    } else {
        // Fallback to POST body
        clientId = req->getParameter("client_id");
        clientSecret = req->getParameter("client_secret");
    }
    
    return {clientId, clientSecret};
}
```

### 3.3 Authentication Validation

```cpp
plugin->validateClient(clientId, clientSecret, [=, callback](bool valid) {
    if (!valid) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k401Unauthorized);
        resp->setBody("{\"error\": \"invalid_client\"}");
        callback(resp);
        return;
    }
    // Continue business logic
});
```

---

## 4. Permission Control Implementation

### 4.1 Revocation Permission Flow

**Strict Permission Control**: Only allow token owner to revoke token

```cpp
// Token revocation permission check
plugin->introspectToken(token, [=, callback, requestingClientId](auto introspection) {
    if (!introspection || !introspection.active) {
        // Token doesn't exist or inactive, return success (RFC 7009)
        callback(createSuccessResponse());
        return;
    }
    
    // Check permission: only token owner can revoke
    if (introspection.clientId != requestingClientId) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k403Forbidden);
        resp->setBody("{\"error\": \"unauthorized_client\", \"error_description\": \"This client is not allowed to revoke the token\"}");
        callback(resp);
        return;
    }
    
    // Has permission, execute revocation
    plugin->revokeAccessToken(token, requestingClientId, [=, callback]() {
        callback(createSuccessResponse());
    });
});
```

### 4.2 Error Response Codes

| HTTP Status | Error Code | Scenario |
|-------------|------------|----------|
| 401 | `invalid_client` | Client authentication failed |
| 400 | `invalid_request` | Missing required parameters |
| 403 | `unauthorized_client` | No permission to revoke token |
| 200 | - | Success (no response body) |

---

## 5. Error Handling and Monitoring

### 5.1 Hybrid Error Handling Strategy

```cpp
private:
void createErrorResponse(
    std::function<void(const HttpResponsePtr &)> &&callback,
    const std::string &errorCode,
    const std::string &description = "",
    drogon::HttpStatusCode statusCode = k400BadRequest)
{
    Json::Value json;
    json["error"] = errorCode;
    if (!description.empty()) {
        json["error_description"] = description;
    }
    
    auto resp = HttpResponse::newHttpJsonResponse(json);
    resp->setStatusCode(statusCode);
    callback(resp);
}

static HttpResponsePtr createSuccessResponse()
{
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k200OK);
    return resp;
}
```

### 5.2 Monitoring Integration

**Reuse existing OAuth2Metrics**:

```cpp
#include "../plugins/OAuth2Metrics.h"

// On introspect success
OAuth2Metrics::incrementIntrospectRequests(clientId);

// On introspect failure
OAuth2Metrics::incrementIntrospectErrors(clientId, "invalid_token");

// On revoke success
OAuth2Metrics::incrementRevocationRequests(clientId);

// Optional: Record P1 specific metrics
OAuth2Metrics::recordTokenIntrospectionTime(clientId, durationMs);
OAuth2Metrics::recordTokenRevocationTime(clientId, durationMs);
```

---

## 6. Testing Strategy

### 6.1 Unit Test Structure

**Create `P1FunctionalityTest.cc`**, following `P0FunctionalityTest.cc` pattern:

```cpp
class P1FunctionalityTest : public ::testing::Test
{
  protected:
    void SetUp() override;
    void TearDown() override;
    
    OAuth2Plugin* plugin_;
    std::shared_ptr<MemoryOAuth2Storage> storage_;
    std::string testClientId_ = "test-client";
    std::string testClientSecret_ = "test-secret";
};

// Test case organization
TEST_F(P1FunctionalityTest, IntrospectValidToken) { }
TEST_F(P1FunctionalityTest, IntrospectInvalidToken) { }
TEST_F(P1FunctionalityTest, IntrospectExpiredToken) { }
TEST_F(P1FunctionalityTest, IntrospectRevokedToken) { }
TEST_F(P1FunctionalityTest, RevokeAccessToken) { }
TEST_F(P1FunctionalityTest, RevokeRefreshToken) { }
TEST_F(P1FunctionalityTest, RevokeUnauthorizedToken) { }
TEST_F(P1FunctionalityTest, ClientAuthentication) { }
```

### 6.2 Test Coverage

**Token Introspection Tests**:
- Valid token introspection
- Invalid/non-existent token
- Expired token
- Revoked token
- Client authentication success/failure
- Missing parameter errors
- P1 field completeness verification

**Token Revocation Tests**:
- Revoke valid token
- Revoke non-existent token (returns 200)
- Permission control (only owner can revoke)
- Audit fields correctly recorded
- Client authentication validation
- Error handling

### 6.3 Test Data Preparation

```cpp
void P1FunctionalityTest::SetUp() {
    // Initialize storage
    storage_ = std::make_shared<MemoryOAuth2Storage>();
    storage_->initFromConfig(clientsConfig, adminConfig);
    
    // Create test token
    OAuth2AccessToken token;
    token.token = "valid-access-token";
    token.clientId = testClientId_;
    token.userId = "user123";
    token.scope = "openid profile";
    token.expiresAt = getCurrentTimestamp() + 3600;
    token.issuedAt = getCurrentTimestamp();
    token.issuer = "https://oauth.example.com";
    // ... Set other P1 fields
    
    storage_->saveAccessToken(token, [](){});
}
```

---

## 7. Implementation Details

### 7.1 File Modification List

**Files to modify**:
1. `OAuth2Controller.h` - Add two new method declarations
2. `OAuth2Controller.cc` - Implement two new methods
3. `OAuth2Controller.cc` - Add P1 route registration
4. `test/P1FunctionalityTest.cc` - Create comprehensive unit tests
5. `OAuth2Metrics.h/.cc` - Optionally add P1 specific metrics

### 7.2 Method Signatures

```cpp
// Add to OAuth2Controller.h
public:
    void introspect(const HttpRequestPtr &req, 
                    std::function<void(const HttpResponsePtr &)> &&callback);
    
    void revoke(const HttpRequestPtr &req, 
                 std::function<void(const HttpResponsePtr &)> &&callback);

private:
    // P1 helper methods
    std::pair<std::string, std::string> extractClientCredentials(const HttpRequestPtr &req);
    
    static HttpResponsePtr createSuccessResponse();
    void createErrorResponse(std::function<void(const HttpResponsePtr &)> &&callback,
                          const std::string &errorCode,
                          const std::string &description = "",
                          drogon::HttpStatusCode statusCode = k400BadRequest);
```

### 7.3 Code Organization Principles

**Follow existing project patterns**:
- Use async callback style (consistent with existing methods)
- Error handling using `createErrorResponse` helper method
- Reuse `OAuth2Plugin` interface, no direct storage layer access
- Use `OAuth2Metrics` for metrics
- Logging using `LOG_DEBUG`/`LOG_INFO`/`LOG_ERROR`

### 7.4 Dependency Injection

```cpp
void OAuth2Controller::introspect(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback)
{
    auto plugin = drogon::app().getPlugin<OAuth2Plugin>();
    if (!plugin) {
        createErrorResponse(callback, "server_error", "OAuth2 plugin not available", 
                         k500InternalServerError);
        return;
    }
    // ... Continue processing
}
```

---

## 8. Performance and Security

### 8.1 Performance Optimization

**Database query optimization**:
- Phase 1 added `idx_access_tokens_token` index
- Use `COALESCE` for backward compatibility, avoid multiple queries
- Atomic introspectCount increment, avoid race conditions

**Caching strategy**:
- Introspection results not cached (real-time requirement)
- Client authentication can be short-term cached (reduce DB queries)

**Timeout handling**:
```cpp
plugin->introspectToken(token, [=, callback](auto introspection) {
    if (!introspection) {
        // Query timeout or failure, return active=false
        TokenIntrospection fallback;
        fallback.active = false;
        callback(createJsonResponse(fallback));
        return;
    }
    // ... Normal processing
});
```

### 8.2 Security Hardening

**Prevent common attacks**:

1. **Token probing attack** - Revocation always returns 200 OK
2. **Timing attack** - Use constant-time comparison for client secrets
3. **Brute force** - Rely on existing Hodor rate limiting
4. **SQL injection** - Use ORM parameterized queries
5. **Information disclosure** - Error responses don't leak sensitive info

**Logging security**:
```cpp
// [PASS] Correct: Don't log sensitive information
LOG_DEBUG << "Token introspection requested by client: " << clientId;

// [ERROR] Wrong: Logging token value
LOG_DEBUG << "Token introspection for: " << token;  // Dangerous!
```

### 8.3 Monitoring and Alerting

**Key metrics**:
- Introspection request frequency (by client)
- Revocation operation frequency (by client)
- Authentication failure rate
- Token query performance (P99 latency)

**Alert thresholds**:
- Single client > 100 requests per minute
- Authentication failure rate > 10%
- Token query timeout rate > 1%

---

## 9. Implementation Plan

### 9.1 Development Phases

**Phase 1: Controller Basic Implementation** (2-3 hours)
- Add P1 route registration
- Implement `extractClientCredentials()` helper method
- Implement `createErrorResponse()` and `createSuccessResponse()` helper methods
- Implement `introspect()` method framework
- Implement `revoke()` method framework

**Phase 2: Business Logic Implementation** (3-4 hours)
- Complete introspect full business logic
- Complete revoke full business logic and permission checking
- Integrate OAuth2Metrics monitoring
- Add logging
- Basic functionality verification

**Phase 3: Unit Testing** (2-3 hours)
- Create `P1FunctionalityTest.cc`
- Implement introspect related test cases
- Implement revoke related test cases
- Implement authentication and permission test cases
- Test coverage verification

**Phase 4: Integration and Optimization** (1-2 hours)
- Compile verification
- Run complete test suite
- Performance benchmarking
- Code review and optimization
- Documentation updates

### 9.2 Quality Checklist

**Code quality**:
- Follow project C++ coding standards
- All async callbacks properly handle lifecycle
- Error handling covers all branches completely
- Logging is appropriate and secure (no sensitive data leakage)
- Pass clang-format and clang-tidy checks

**Test quality**:
- Unit test coverage ≥ 80%
- All P1 functional paths have corresponding tests
- Error scenario testing complete
- Boundary condition test coverage

**Integration quality**:
- No conflicts with existing OAuth2 functionality
- Pass all existing test suites
- Performance metrics meet requirements (< 100ms P99)
- Memory leak checks pass

### 9.3 Acceptance Criteria

**Functional acceptance**:
- `/oauth2/introspect` endpoint complies with RFC 7662
- `/oauth2/revoke` endpoint complies with RFC 7009
- Support both client authentication methods
- Strict permission control (only token owner can revoke)

**Quality acceptance**:
- All unit tests pass
- Existing test suites have no regression
- Code passes static analysis
- No memory leaks

**Documentation acceptance**:
- API documentation updated
- Test documentation complete
- Design document archived

---

## 10. Risk Assessment and Mitigation

### 10.1 Identified Risks

| Risk | Impact | Probability | Mitigation |
|------|--------|------------|------------|
| OAuth2Controller file becomes too large | Maintainability | Medium | Consider future refactoring to extract P1 functionality |
| Performance degradation with many introspect requests | User experience | Low | Database indexes and caching in Phase 1 |
| Client authentication bypass | Security | Low | Strict validation, constant-time comparison |
| Token revocation race conditions | Data consistency | Low | Atomic database operations, proper error handling |

### 10.2 Rollback Plan

If critical issues are discovered:
- P1 endpoints can be disabled via configuration
- Revert commits are straightforward (isolated changes)
- Existing P0 functionality remains unaffected

---

## Appendix A: RFC Compliance Reference

### RFC 7662 Token Introspection

**Key requirements implemented**:
- Section 2.1: Introspection request format
- Section 2.2: Introspection response format
- Section 2.2.1: Active token response
- Section 2.2.2: Inactive token response
- Section 3: Client authentication requirements

### RFC 7009 Token Revocation

**Key requirements implemented**:
- Section 2.1: Revocation request format
- Section 2.2: Revocation response format
- Section 2.1: Token type hint parameter
- Section 3: Client authentication requirements
- Section 4.1: Successful revocation response
- Section 4.2: Error responses

---

## Appendix B: Code Examples

### Example 1: Introspect Valid Token

**Request**:
```bash
POST /oauth2/introspect HTTP/1.1
Host: oauth.example.com
Authorization: Basic dmxlLWNsaWVudDpzZWNyZXQ=
Content-Type: application/x-www-form-urlencoded

token=SlAV32hkKG
```

**Response**:
```json
HTTP/1.1 200 OK
Content-Type: application/json

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
  "scope": "openid profile"
}
```

### Example 2: Revoke Token

**Request**:
```bash
POST /oauth2/revoke HTTP/1.1
Host: oauth.example.com
Authorization: Basic dmxlLWNsaWVudDpzZWNyZXQ=
Content-Type: application/x-www-form-urlencoded

token=SlAV32hkKG
```

**Response**:
```json
HTTP/1.1 200 OK
Content-Length: 0
```

---

**Document Status**: Ready for Implementation
**Next Step**: Invoke writing-plans skill to create detailed implementation plan
