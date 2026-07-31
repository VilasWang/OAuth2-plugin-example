# OAuth2 Plugin - Common Utilities Refactoring Design

**Date**: 2026-04-27
**Author**: Claude Code
**Status**: Approved
**Version**: 1.0

---

## Executive Summary

This design document outlines the refactoring of the OAuth2 Plugin Example project to introduce a `common/` directory with three core utility components: ConfigManager, ErrorHandler, and Validator. This refactoring will improve code maintainability, enhance security through input validation, and provide automated API documentation.

**Key Objectives**:
1. Create a reusable `common/` layer for shared utilities
2. Refactor configuration management in main.cc (reduce code by 80%)
3. Implement structured error handling across the application
4. Add OAuth2-specific input validation for security
5. Integrate OpenAPI/Swagger for automated API documentation

**Implementation Timeline**: 7-11 days across 5 phases

---

## Architecture Overview

### Current State
- Configuration logic scattered in main.cc (~90 lines)
- Inconsistent error handling across storage implementations
- Missing input validation in controllers
- Manual API documentation
- No shared utility layer

### Target State

```
OAuth2Backend/
├── common/                    # NEW: Shared utility layer
│   ├── config/
│   │   ├── ConfigManager.h/cc
│   │   └── ConfigTypes.h
│   ├── error/
│   │   ├── ErrorHandler.h/cc
│   │   └── ErrorTypes.h
│   └── validation/
│       ├── Validator.h/cc
│       └── ValidationRules.h
├── controllers/               # Enhanced: Use common utilities
├── plugins/
├── storage/
├── docs/
│   └── api/                   # NEW: API documentation
│       ├── openapi.json
│       └── swagger-ui/
└── main.cc                    # Refactored: Use ConfigManager
```

### Design Principles
- **Separation of Concerns**: Common layer independent of business logic
- **Type Safety**: Template-based configuration access
- **Consistency**: Unified error handling and validation
- **Backward Compatibility**: No breaking changes to existing APIs
- **Testability**: Each component independently testable

---

## Component 1: ConfigManager

### Purpose
Encapsulate configuration loading, environment variable overrides, and validation logic.

### Interface

```cpp
namespace common::config {

class ConfigManager {
public:
    // Load config file with environment variable overrides
    static bool load(const std::string& configPath, Json::Value& config);

    // Type-safe configuration access
    template<typename T>
    static T get(const Json::Value& config, const std::string& path,
                 const T& defaultValue = T{});

    // Configuration validation
    static bool validate(const Json::Value& config, std::string& errorMessage);

    // Environment variable override structure
    struct EnvOverride {
        std::string configPath;    // JSON path like "db_clients.0.host"
        const char* envVar;         // Environment variable name
        bool isNumeric;            // Is numeric type
    };

    static void applyEnvOverrides(Json::Value& config,
                                  const std::vector<EnvOverride>& rules);

private:
    static Json::Value* getJsonPointer(Json::Value& root, const std::string& path);
};

} // namespace common::config
```

### Environment Variable Override Rules

```cpp
const std::vector<EnvOverride> OAUTH2_ENV_OVERRIDES = {
    {"db_clients.0.host", "OAUTH2_DB_HOST", false},
    {"db_clients.0.port", "OAUTH2_DB_PORT", true},
    {"db_clients.0.name", "OAUTH2_DB_NAME", false},
    {"db_clients.0.user", "OAUTH2_DB_USER", false},
    {"db_clients.0.passwd", "OAUTH2_DB_PASSWORD", false},
    {"redis.host", "OAUTH2_REDIS_HOST", false},
    {"redis.port", "OAUTH2_REDIS_PORT", true},
    {"redis.password", "OAUTH2_REDIS_PASSWORD", false},
    {"vue_client.secret", "OAUTH2_VUE_CLIENT_SECRET", false}
};
```

### Usage Example

```cpp
// In main.cc
Json::Value config;
if (!common::config::ConfigManager::load("config.json", config)) {
    LOG_ERROR << "Failed to load config";
    return 1;
}

std::string errMsg;
if (!common::config::ConfigManager::validate(config, errMsg)) {
    LOG_ERROR << "Config validation failed: " << errMsg;
    return 1;
}

// Type-safe access
auto dbHost = common::config::ConfigManager::get<std::string>(
    config, "db_clients.0.host");
auto dbPort = common::config::ConfigManager::get<int>(
    config, "db_clients.0.port", 5432);
```

### Benefits
- **Code Reduction**: 90 lines → 15 lines in main.cc
- **Type Safety**: Compile-time type checking for config access
- **Validation**: Prevents runtime errors from invalid configuration
- **Maintainability**: Centralized config logic

---

## Component 2: ErrorHandler

### Purpose
Provide unified exception handling, error categorization, and structured error responses.

### Error Classification

```cpp
namespace common::error {

enum class ErrorCategory {
    NETWORK,        // Network-related errors
    DATABASE,       // Database errors
    VALIDATION,     // Input validation errors
    AUTHENTICATION, // Authentication errors
    AUTHORIZATION,  // Authorization errors
    INTERNAL,       // Internal system errors
    UNKNOWN         // Unknown errors
};

enum class ErrorCode {
    // Network (1000-1099)
    CONNECTION_FAILED = 1001,
    TIMEOUT = 1002,

    // Database (2000-2099)
    DB_CONNECTION_ERROR = 2001,
    DB_QUERY_ERROR = 2002,
    DB_CONSTRAINT_VIOLATION = 2003,

    // Validation (3000-3099)
    INVALID_INPUT = 3001,
    MISSING_REQUIRED_FIELD = 3002,
    FORMAT_ERROR = 3003,

    // Authentication (4000-4099)
    INVALID_CREDENTIALS = 4001,
    TOKEN_EXPIRED = 4002,
    TOKEN_INVALID = 4003,

    // Authorization (5000-5099)
    ACCESS_DENIED = 5001,
    INSUFFICIENT_PERMISSIONS = 5002
};

struct Error {
    ErrorCode code;
    ErrorCategory category;
    std::string message;
    std::string details;
    std::string requestId;

    int toHttpStatusCode() const;
    Json::Value toJson() const;

    static Error fromException(const std::exception& e,
                               ErrorCategory category);
};

} // namespace common::error
```

### Interface

```cpp
class ErrorHandler {
public:
    template<typename Func>
    static auto handle(Func&& func,
                      std::function<void(const Error&)> callback) -> void;

    static Error handleDbException(const DrogonDbException& e);
    static Error handleValidationError(const std::string& field,
                                      const std::string& reason);
    static std::string generateRequestId();
    static void logError(const Error& error,
                        const std::string& context = "");
};
```

### Usage Example

```cpp
// In OAuth2Controller
void OAuth2Controller::token(const HttpRequestPtr& req,
                            std::function<void(const HttpResponsePtr&)>&& callback) {
    common::error::ErrorHandler::handle([&]() {
        std::string clientId = req->getParameter("client_id");
        if (clientId.empty()) {
            throw common::error::Error{
                common::error::ErrorCode::MISSING_REQUIRED_FIELD,
                common::error::ErrorCategory::VALIDATION,
                "client_id is required",
                "field: client_id"
            };
        }
        // ... business logic
    }, [&](const common::error::Error& error) {
        common::error::ErrorHandler::logError(error, "OAuth2Controller::token");

        Json::Value errorJson = error.toJson();
        auto resp = HttpResponse::newHttpJsonResponse(errorJson);
        resp->setStatusCode(error.toHttpStatusCode());
        callback(resp);
    });
}
```

### Error Response Format

```json
{
  "error": {
    "code": 3002,
    "category": "VALIDATION",
    "message": "Missing required field",
    "details": "field: client_id",
    "request_id": "req_abc123xyz"
  }
}
```

### Benefits
- **Consistency**: Unified error format across all endpoints
- **Debugging**: Request tracking and structured error details
- **Monitoring**: Error categorization for metrics
- **Client Experience**: Clear error messages and codes

---

## Component 3: Validator

### Purpose
Provide input validation with OAuth2-specific validation rules to enhance security.

### Interface

```cpp
namespace common::validation {

enum class ValidationRule {
    NOT_EMPTY,
    LENGTH_LIMIT,
    REGEX_PATTERN,
    NUMERIC_RANGE,
    URL_FORMAT,
    EMAIL_FORMAT
};

struct ValidationResult {
    bool isValid;
    std::string fieldName;
    std::string errorMessage;

    static ValidationResult success();
    static ValidationResult failure(const std::string& field,
                                   const std::string& message);
};

class Validator {
public:
    // Basic validation methods
    static ValidationResult notEmpty(const std::string& value,
                                     const std::string& fieldName);
    static ValidationResult length(const std::string& value,
                                   const std::string& fieldName,
                                   size_t minLen, size_t maxLen);
    static ValidationResult regex(const std::string& value,
                                  const std::string& fieldName,
                                  const std::string& pattern);
    static ValidationResult numericRange(int value,
                                        const std::string& fieldName,
                                        int minVal, int maxVal);

    // OAuth2-specific validation
    static ValidationResult validateClientId(const std::string& clientId);
    static ValidationResult validateClientSecret(const std::string& secret);
    static ValidationResult validateRedirectUri(const std::string& uri);
    static ValidationResult validateScope(const std::string& scope);
    static ValidationResult validateResponseType(const std::string& type);
    static ValidationResult validateGrantType(const std::string& type);
    static ValidationResult validateToken(const std::string& token);

    // Batch validation
    static std::vector<ValidationResult> validateAll(
        const std::vector<std::pair<std::string, std::string>>& fieldsAndValues,
        const std::vector<ValidationRule>& rules
    );
};

} // namespace common::validation
```

### OAuth2 Validation Rules

```cpp
// Client ID: 1-128 alphanumeric characters (._- allowed)
ValidationResult Validator::validateClientId(const std::string& clientId) {
    if (!regex(clientId, "client_id", "^[a-zA-Z0-9._-]{1,128}$").isValid) {
        return ValidationResult::failure("client_id",
            "Must be 1-128 alphanumeric characters (._- allowed)");
    }
    return ValidationResult::success();
}

// Redirect URI: Valid HTTP/HTTPS URL, 10-2048 characters
ValidationResult Validator::validateRedirectUri(const std::string& uri) {
    if (!regex(uri, "redirect_uri", "^https?://[^\\s/$.?#].[^\\s]*$").isValid) {
        return ValidationResult::failure("redirect_uri",
            "Must be a valid HTTP/HTTPS URL");
    }
    return length(uri, "redirect_uri", 10, 2048);
}

// Token: At least 32 characters, alphanumeric plus ._-
ValidationResult Validator::validateToken(const std::string& token) {
    if (token.length() < 32) {
        return ValidationResult::failure("token",
            "Token must be at least 32 characters");
    }
    if (!regex(token, "token", "^[a-zA-Z0-9._-]+$").isValid) {
        return ValidationResult::failure("token",
            "Token contains invalid characters");
    }
    return ValidationResult::success();
}
```

### Usage Example

```cpp
// In OAuth2Controller
void OAuth2Controller::authorize(const HttpRequestPtr& req,
                                 std::function<void(const HttpResponsePtr&)>&& callback) {
    std::string clientId = req->getParameter("client_id");
    std::string redirectUri = req->getParameter("redirect_uri");
    std::string responseType = req->getParameter("response_type");

    // Validate parameters
    using namespace common::validation;
    auto result1 = Validator::validateClientId(clientId);
    if (!result1.isValid) {
        return errorResponse(callback, result1.errorMessage, 400);
    }

    auto result2 = Validator::validateRedirectUri(redirectUri);
    if (!result2.isValid) {
        return errorResponse(callback, result2.errorMessage, 400);
    }

    auto result3 = Validator::validateResponseType(responseType);
    if (!result3.isValid) {
        return errorResponse(callback, result3.errorMessage, 400);
    }

    // ... continue business logic
}
```

### Benefits
- **Security**: Prevents injection attacks through input validation
- **User Experience**: Clear validation error messages
- **Compliance**: OAuth2 RFC compliance checks
- **Maintainability**: Centralized validation logic

---

## Component 4: API Documentation Automation

### Purpose
Automatically generate OpenAPI/Swagger documentation from code annotations.

### Architecture

```
docs/
├── api/
│   ├── openapi.json       # Auto-generated OpenAPI spec
│   └── swagger-ui/        # Swagger UI static files
└── ...

common/
└── documentation/
    ├── OpenApiGenerator.h
    └── ApiDocHelper.h
```

### Interface

```cpp
namespace common::documentation {

struct EndpointInfo {
    std::string path;
    std::string method;
    std::string summary;
    std::string description;
    std::vector<std::string> tags;
    std::map<std::string, std::string> parameters;
    std::map<int, std::string> responses;
    bool requiresAuth;
};

class OpenApiGenerator {
public:
    static void addEndpoint(const EndpointInfo& endpoint);
    static Json::Value generateOpenApiSpec();
    static bool writeToFile(const std::string& outputPath);
    static void setApiInfo(const std::string& title,
                          const std::string& version,
                          const std::string& description);

private:
    static std::vector<EndpointInfo> endpoints_;
    static Json::Value apiInfo_;
};

} // namespace common::documentation
```

### Usage Example

```cpp
// In OAuth2Controller::init()
void OAuth2Controller::init() {
    using namespace common::documentation;

    OpenApiGenerator::addEndpoint({
        .path = "/oauth2/token",
        .method = "POST",
        .summary = "Exchange authorization code for access token",
        .description = "OAuth2 token endpoint - exchanges authorization "
                      "code or refresh token for access token",
        .tags = {"OAuth2", "Token"},
        .parameters = {
            {"grant_type", "Authorization code or refresh token (required)"},
            {"code", "Authorization code (required for "
                     "grant_type=authorization_code)"},
            {"refresh_token", "Refresh token (required for "
                            "grant_type=refresh_token)"},
            {"client_id", "Client identifier (required)"},
            {"client_secret", "Client secret (required for "
                            "confidential clients)"},
            {"redirect_uri", "Redirect URI (required for "
                           "authorization_code grant)"}
        },
        .responses = {
            {200, "Token response with access_token and refresh_token"},
            {400, "Invalid request"},
            {401, "Authentication failed"}
        },
        .requiresAuth = false
    });
}
```

### Swagger UI Integration

```cpp
// In main.cc
#include "common/documentation/OpenApiGenerator.h"

int main(int argc, char *argv[]) {
    // ... config loading

    // Generate OpenAPI specification
    if (!common::documentation::OpenApiGenerator::writeToFile(
            "docs/api/openapi.json")) {
        LOG_WARN << "Failed to write OpenAPI specification";
    }

    // Serve Swagger UI
    drogon::app().registerStaticFileRouter("/api-docs",
                                           "docs/api/swagger-ui");

    // ... start application
}
```

### Access Documentation
- Swagger UI: `http://localhost:8080/api-docs/`
- OpenAPI JSON: `http://localhost:8080/api-docs/openapi.json`

### Benefits
- **Developer Experience**: Interactive API exploration
- **Team Collaboration**: Shared API specification
- **Client Generation**: Can generate client SDKs from spec
- **Documentation as Code**: Always up-to-date with implementation

---

## Testing Strategy

### Test Structure

```
test/
├── common/                    # NEW: Common component tests
│   ├── ConfigManagerTest.cc
│   ├── ErrorHandlerTest.cc
│   └── ValidatorTest.cc
├── ConfigMigrationTest.cc     # NEW: Migration verification
├── PluginTest.cc             # Existing tests
├── FunctionalTest.cc         # Existing tests
└── ...
```

### Test Coverage Requirements

**ConfigManager Tests**:
- Load valid configuration
- Environment variable overrides
- Configuration validation (missing fields, invalid values)
- Type-safe access with defaults
- JSON path resolution

**ErrorHandler Tests**:
- Database exception to Error conversion
- Error code to HTTP status code mapping
- Error to JSON serialization
- Request ID generation
- Error logging

**Validator Tests**:
- Valid/invalid client ID formats
- Redirect URI validation (HTTP/HTTPS, length)
- Token validation (length, character set)
- Scope format validation
- Batch validation

**Migration Tests**:
- main.cc config load functionality
- Environment variable overrides consistency
- Backward compatibility verification

### Coverage Targets
- Common components: ≥90%
- Overall: Maintain current level, aim for improvement

---

## Implementation Phases

### Phase 1: Create Common Directory (1-2 days)
**Tasks**:
- [ ] Create directory structure `common/{config,error,validation}/`
- [ ] Implement ConfigManager.h/cc
- [ ] Implement ErrorHandler.h/cc
- [ ] Implement Validator.h/cc
- [ ] Add unit tests for each component
- [ ] Run tests to ensure passing

**Acceptance Criteria**:
- All new tests pass
- Code follows project conventions
- No impact on existing functionality

---

### Phase 2: Refactor Configuration Management (1 day)
**Tasks**:
- [ ] Modify main.cc to include ConfigManager
- [ ] Replace loadConfigWithEnv function
- [ ] Add configuration validation
- [ ] Add migration tests
- [ ] Run complete test suite
- [ ] Update documentation

**Acceptance Criteria**:
- All existing tests pass
- Environment variable overrides work correctly
- Configuration validation works properly
- main.cc code lines reduced by 80%

---

### Phase 3: Integrate Validator and ErrorHandler (2-3 days)
**Tasks**:
- [ ] Integrate Validator in OAuth2Controller
- [ ] Integrate Validator in AdminController
- [ ] Add ErrorHandler to Controller layer
- [ ] Update error response format
- [ ] Add integration tests
- [ ] Update API documentation examples

**Acceptance Criteria**:
- Input validation blocks invalid requests
- Error response format is consistent
- Security tests pass
- Existing functionality unaffected

---

### Phase 4: API Documentation Automation (2-3 days)
**Tasks**:
- [ ] Implement OpenApiGenerator
- [ ] Add documentation annotations in Controllers
- [ ] Integrate Swagger UI
- [ ] Generate OpenAPI specification
- [ ] Add documentation endpoint
- [ ] Test documentation accessibility

**Acceptance Criteria**:
- Swagger UI accessible at `http://localhost:8080/api-docs`
- OpenAPI specification generates correctly
- Documentation examples are complete
- Documentation matches actual API

---

### Phase 5: Validation and Optimization (1-2 days)
**Tasks**:
- [ ] Run complete test suite
- [ ] Performance benchmark testing
- [ ] Code review
- [ ] Documentation refinement
- [ ] Cleanup temporary code
- [ ] Submit pull request

**Acceptance Criteria**:
- All tests pass (100%)
- No significant performance degradation
- Code review approved
- Documentation fully updated

---

## Risk Management

| Risk | Impact | Probability | Mitigation |
|------|--------|-------------|------------|
| Config loading breaks existing functionality | High | Low | Comprehensive test coverage, quick rollback |
| Validator incorrectly rejects valid requests | Medium | Medium | Extensive testing, gradual enablement |
| OpenAPI documentation inconsistent with API | Low | Medium | Automated validation in CI |
| Performance regression | Medium | Low | Benchmark testing, performance monitoring |

---

## Expected Benefits

### Code Quality
- **Configuration code reduced by 80%** (90 lines → 15 lines)
- **Unified error handling** improves debugging experience
- **Enhanced security** through input validation
- **Reduced code duplication** across components

### Developer Experience
- **Type-safe configuration access** prevents runtime errors
- **Interactive API documentation** accelerates development
- **Clear error messages** improve troubleshooting
- **Modular architecture** simplifies maintenance

### Maintainability
- **Modular architecture** with clear boundaries
- **Comprehensive test coverage** ensures reliability
- **Automated documentation** stays in sync with code
- **Extensible design** for future enhancements

### Performance
- **Minimal overhead**: Common components are lightweight
- **No degradation**: Existing performance maintained
- **Monitoring ready**: Error categorization enables metrics

---

## Success Metrics

- ✅ All 63 existing tests pass (100% pass rate maintained)
- ✅ New common components achieve ≥90% test coverage
- ✅ Configuration code in main.cc reduced from 90 to 15 lines
- ✅ Input validation blocks all identified security vulnerabilities
- ✅ API documentation accessible and accurate
- ✅ No performance degradation (<5% variance)
- ✅ Code review approved by maintainers

---

## Future Considerations

### Short-term (Next 3 months)
- Add configuration hot-reload capability
- Implement retry mechanisms in ErrorHandler
- Extend Validator with additional OAuth2 flows
- Add API usage analytics

### Long-term (6-12 months)
- Configuration encryption for sensitive fields
- Distributed tracing integration
- Advanced rate limiting with circuit breakers
- Multi-environment configuration management

---

## Conclusion

This refactoring introduces a robust common utility layer that significantly improves code quality, security, and developer experience. The phased implementation approach minimizes risk while delivering immediate value at each stage.

The modular design ensures backward compatibility and provides a solid foundation for future enhancements. With comprehensive test coverage and clear documentation, this refactoring will be maintainable and extensible for years to come.

**Total Estimated Timeline**: 7-11 days
**Risk Level**: Low (comprehensive testing, phased rollout)
**ROI**: High (immediate benefits in code quality and security)

---

## Appendix

### A. Dependencies
- No new external dependencies required
- Uses existing Drogon framework features
- Leverages standard C++17/20 library

### B. Compatibility
- Fully compatible with existing OAuth2 flows
- No breaking changes to public APIs
- Backward compatible with existing configuration files

### C. References
- OAuth2.0 RFC 6749: https://tools.ietf.org/html/rfc6749
- OpenAPI 3.0 Specification: https://swagger.io/specification/
- Drogon Framework Documentation: https://drogon.docs.drogonframework.com/
