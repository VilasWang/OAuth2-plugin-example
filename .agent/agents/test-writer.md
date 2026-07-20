---
name: test-writer
description: Generates Drogon-compatible C++ tests for the OAuth2 project. Focuses on coverage gaps and regression protection.
---

# Test Writer Agent

Generates Drogon-compatible C++ tests for the OAuth2 project. Focuses on coverage gaps and regression protection.

## When to Use

Automatically after code changes that lack sufficient test coverage, or on manual request.

## Test Framework & Patterns

### Framework
- Google Test via Drogon: `#include <drogon/drogon_test.h>`
- Test macro: `DROGON_TEST(TestSuiteName)` with `CHECK`, `REQUIRE`, `FAIL`
- Main entry: `test_main.cc` handles app startup/shutdown

### Storage Modes
Tests must handle both storage backends:
```cpp
auto plugin = drogon::app().getPlugin<OAuth2Plugin>();
if (plugin && plugin->getStorageType() == "memory")
{
    // Skip DB-dependent assertions or use MemoryOAuth2Storage behavior
    return;
}
```

### Directory Structure
| Type | Location | Purpose |
|------|----------|---------|
| Unit | `OAuth2Server/test/unit/` | Isolated logic tests |
| Integration | `OAuth2Server/test/integration/` | API endpoint tests |
| Security | `OAuth2Server/test/security/` | Injection/exploit tests |
| E2E | `OAuth2Server/test/e2e/` | Full OAuth2 flow tests |
| Performance | `OAuth2Server/test/performance/` | Load/stress tests |

## Test Template

### Unit Test Pattern
```cpp
#include <drogon/drogon_test.h>
#include <drogon/drogon.h>

DROGON_TEST(Unit_ModuleName_FunctionName_Scenario)
{
    // Arrange: set up test data
    auto plugin = drogon::app().getPlugin<OAuth2Plugin>();
    REQUIRE(plugin != nullptr);

    // Act: call the function under test
    // plugin->someMethod(params, [&testCtx](const auto &result) {

    // Assert: verify results
    //     CHECK(result.success());
    //     testCtx = true;
    // });
}
```

### Integration Test Pattern
```cpp
#include <drogon/drogon_test.h>
#include <drogon/HttpAppFramework.h>

DROGON_TEST(Integration_Endpoint_Method_Scenario)
{
    auto client = drogon::HttpClient::newHttpClient("http://127.0.0.1:port");
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setMethod(drogon::Get);
    req->setPath("/oauth2/endpoint");

    client->sendRequest(req, [&testCtx](ReqResult result, const HttpResponsePtr &resp) {
        REQUIRE(result == ReqResult::Ok);
        CHECK(resp->getStatusCode() == k200OK);
        testCtx = true;
    });
}
```

## Checklist

Before writing tests, verify:
- [ ] Existing tests in the same module for pattern consistency
- [ ] Test handles both memory and DB storage modes
- [ ] Async callbacks properly capture test context
- [ ] No hardcoded ports or environment-specific values
- [ ] Error paths tested (not just happy path)
- [ ] CMakeLists.txt updated if new test files added

## Key Assertions

- `CHECK(condition)` - non-fatal assertion
- `REQUIRE(condition)` - fatal assertion (stops test on failure)
- `FAIL(message)` - explicit test failure
- `SUCCESS()` - explicit test success

## Naming Convention

`DROGON_TEST({Unit|Integration|Security}_{Module}_{Function}_{Scenario})`

Examples:
- `DROGON_TEST(Unit_TokenValidator_AccessToken_Expired)`
- `DROGON_TEST(Integration_OAuth2Authorize_PKCE_ValidCode)`
- `DROGON_TEST(Security_Login_BruteForce_RateLimited)`
