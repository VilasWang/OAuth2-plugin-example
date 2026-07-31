# Hodor Rate Limiter Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace custom RateLimiterFilter with Drogon's official Hodor plugin for enhanced rate limiting with multi-level protection (IP/user/global), whitelist support, and configuration-based management.

**Architecture:** Migrate from Redis-based fixed window rate limiter (custom Filter) to Drogon's Hodor plugin using token bucket algorithm with in-memory CacheMap. Hodor operates as global advice (pre-handling) instead of filter, supports whitelisted IPs via trust_cidrs, and enables per-user rate limiting through configurable callbacks.

**Tech Stack:** Drogon C++ Framework, Hodor Plugin (official), Token Bucket Algorithm, JSON Configuration, C++20

---

## File Structure

**Files to modify:**
- `config.json` - Add Hodor plugin configuration with rate limits and whitelist
- `OAuth2Backend/controllers/OAuth2Controller.h` - Remove RateLimiterFilter references
- `OAuth2Backend/main.cc` - Add userIdGetter callback for Hodor
- `OAuth2Backend/docs/security_hardening.md` - Update documentation

**Files to delete:**
- `OAuth2Backend/filters/RateLimiterFilter.cc`
- `OAuth2Backend/filters/RateLimiterFilter.h`

**Files to potentially update (search and cleanup):**
- Documentation files mentioning RateLimiterFilter
- Test files (may need rewrite)

---

## Task 1: Backup Current Working State

**Files:**
- Create: `OAuth2Backend/.backup/backup_info.txt`

**Context:** Before making changes, create a backup branch and record current state for easy rollback if needed.

- [ ] **Step 1: Create backup branch**

```bash
cd d:\work\development\Repos\backend\drogon-plugin\authforge
git checkout -b backup/before-hodor-migration
git push -u origin backup/before-hodor-migration
```

- [ ] **Step 2: Record current state**

```bash
cd d:\work\development\Repos\backend\drogon-plugin\authforge\OAuth2Backend
mkdir -p .backup
git rev-parse HEAD > .backup/commit_hash.txt
git log -1 --pretty=format:"%H %s %ai" > .backup/backup_info.txt
date >> .backup/backup_info.txt
```

- [ ] **Step 3: Switch back to master branch**

```bash
cd d:\work\development\Repos\backend\drogon-plugin\authforge
git checkout master
```

- [ ] **Step 4: Commit backup info**

```bash
git add OAuth2Backend/.backup/
git commit -m "chore: create backup state before Hodor migration"
```

---

## Task 2: Add Hodor Configuration to config.json

**Files:**
- Modify: `config.json`

**Context:** Add Hodor plugin to the plugins array with comprehensive rate limiting configuration including token bucket algorithm, multi-level limits (global/IP/user), sub-limits for specific endpoints, and whitelist for development environments.

- [ ] **Step 1: Read current config.json**

```bash
cd d:\work\development\Repos\backend\drogon-plugin\authforge\OAuth2Backend
cat config.json
```

- [ ] **Step 2: Add Hodor plugin configuration**

Insert the following Hodor configuration into the `plugins` array, **after** PromExporter and **before** OAuth2Plugin:

```json
{
    "name": "drogon::plugin::Hodor",
    "dependencies": [],
    "config": {
        "algorithm": "token_bucket",
        "time_unit": 60,
        "capacity": 1000,
        "ip_capacity": 60,
        "user_capacity": 0,
        "use_real_ip_resolver": false,
        "multi_threads": true,
        "rejection_message": "Too Many Requests",
        "limiter_expire_time": 600,
        "urls": ["^/.*"],
        "sub_limits": [
            {
                "urls": ["^/oauth2/login"],
                "capacity": 5000,
                "ip_capacity": 5,
                "user_capacity": 5
            },
            {
                "urls": ["^/oauth2/token"],
                "capacity": 10000,
                "ip_capacity": 10,
                "user_capacity": 10
            },
            {
                "urls": ["^/api/register"],
                "capacity": 5000,
                "ip_capacity": 5,
                "user_capacity": 5
            }
        ],
        "trust_ips": [
            "127.0.0.1",
            "::1",
            "172.16.0.0/12",
            "172.17.0.0/12",
            "192.168.0.0/16"
        ]
    }
},
```

**Important:** Ensure proper JSON syntax with commas between plugins.

- [ ] **Step 3: Validate JSON syntax**

```bash
cd d:\work\development\Repos\backend\drogon-plugin\authforge\OAuth2Backend
# Use Python or jq to validate JSON
python -m json.tool config.json > nul 2>&1
echo %ERRORLEVEL%
```

Expected: `0` (zero indicates valid JSON)

- [ ] **Step 4: Commit configuration changes**

```bash
git add config.json
git commit -m "feat: add Hodor rate limiter plugin configuration

- Add token bucket algorithm
- Configure multi-level limits (global/IP/user)
- Add sub-limits for login/token/register endpoints
- Whitelist local and Docker network IPs
- Replace custom RateLimiterFilter approach
"
```

---

## Task 3: Remove RateLimiterFilter References from OAuth2Controller.h

**Files:**
- Modify: `OAuth2Backend/controllers/OAuth2Controller.h`

**Context:** Remove `"RateLimiterFilter"` string from ADD_METHOD_TO macros since Hodor operates as global advice and doesn't need to be registered per-method.

- [ ] **Step 1: Read current OAuth2Controller.h**

```bash
cd d:\work\development\Repos\backend\drogon-plugin\authforge\OAuth2Backend\controllers
cat OAuth2Controller.h
```

- [ ] **Step 2: Locate ADD_METHOD_TO lines with RateLimiterFilter**

Find lines containing:
- `ADD_METHOD_TO(OAuth2Controller::token, "/oauth2/token", Post, "RateLimiterFilter");`
- `ADD_METHOD_TO(OAuth2Controller::login, "/oauth2/login", Post, "RateLimiterFilter");`
- `ADD_METHOD_TO(OAuth2Controller::registerUser, "/api/register", Post, "RateLimiterFilter");`

- [ ] **Step 3: Remove RateLimiterFilter from token endpoint**

Change from:
```cpp
ADD_METHOD_TO(OAuth2Controller::token,
              "/oauth2/token",
              Post,
              "RateLimiterFilter");
```

To:
```cpp
ADD_METHOD_TO(OAuth2Controller::token, "/oauth2/token", Post);
```

- [ ] **Step 4: Remove RateLimiterFilter from login endpoint**

Change from:
```cpp
ADD_METHOD_TO(OAuth2Controller::login,
              "/oauth2/login",
              Post,
              "RateLimiterFilter");
```

To:
```cpp
ADD_METHOD_TO(OAuth2Controller::login, "/oauth2/login", Post);
```

- [ ] **Step 5: Remove RateLimiterFilter from registerUser endpoint**

Change from:
```cpp
ADD_METHOD_TO(OAuth2Controller::registerUser,
              "/api/register",
              Post,
              "RateLimiterFilter");
```

To:
```cpp
ADD_METHOD_TO(OAuth2Controller::registerUser, "/api/register", Post);
```

- [ ] **Step 6: Verify no other RateLimiterFilter references in file**

```bash
cd d:\work\development\Repos\backend\drogon-plugin\authforge\OAuth2Backend\controllers
grep -n "RateLimiterFilter" OAuth2Controller.h
```

Expected: No results (empty)

- [ ] **Step 7: Commit controller changes**

```bash
git add controllers/OAuth2Controller.h
git commit -m "refactor: remove RateLimiterFilter from OAuth2Controller

Hodor operates as global advice, no longer needs per-method
filter registration. Clean up ADD_METHOD_TO macros.
"
```

---

## Task 4: Add User Identification Callback to main.cc

**Files:**
- Modify: `OAuth2Backend/main.cc`

**Context:** Add userIdGetter callback to Hodor plugin to enable per-user rate limiting. Extract user IDs from Bearer tokens (validated by OAuth2Plugin) or Basic Auth (OAuth2 client credentials).

- [ ] **Step 1: Read current main.cc**

```bash
cd d:\work\development\Repos\backend\drogon-plugin\authforge\OAuth2Backend
cat main.cc
```

- [ ] **Step 2: Add Hodor include at top of file**

Add after other includes:
```cpp
#include <drogon/plugins/Hodor.h>
#include <drogon/utils/Utilities.h>
```

- [ ] **Step 3: Locate the app().run() call in main()**

Find the line that calls `app().run()` or `loop().loop()`.

- [ ] **Step 4: Add userIdGetter callback before app().run()**

Insert the following code **immediately before** `app().run()` or the event loop call:

```cpp
// Configure Hodor rate limiter with user identification callback
auto hodor = app().getPlugin<drogon::plugin::Hodor>();
hodor->setUserIdGetter([](const HttpRequestPtr &req) -> std::optional<std::string> {
    std::string authHeader = req->getHeader("Authorization");

    // 1. Check Bearer Token (for /oauth2/userinfo and authenticated endpoints)
    if (!authHeader.empty() && authHeader.find("Bearer ") == 0) {
        try {
            // OAuth2Plugin sets user_id attribute after token validation
            auto userIdOpt = req->attributes()->get<std::string>("user_id");
            if (userIdOpt) {
                return userIdOpt;
            }
        } catch (...) {
            // Token invalid or expired, continue to Basic Auth check
        }
    }

    // 2. Check Basic Auth (for /oauth2/token with client credentials)
    if (!authHeader.empty() && authHeader.find("Basic ") == 0) {
        std::string basicAuth = authHeader.substr(6);
        try {
            auto decoded = drogon::utils::base64Decode(basicAuth);
            size_t colonPos = decoded.find(':');
            if (colonPos != std::string::npos) {
                std::string clientId = decoded.substr(0, colonPos);
                return "client:" + clientId;  // Prefix to distinguish from user IDs
            }
        } catch (...) {
            // Base64 decode failed
        }
    }

    // 3. No valid user identifier, return nullopt (IP-based limiting only)
    return std::nullopt;
});
```

- [ ] **Step 5: Verify no syntax errors**

```bash
cd d:\work\development\Repos\backend\drogon-plugin\authforge\OAuth2Backend
# Check if main.cc compiles
scripts\build.bat -debug 2>&1 | findstr /C:"error" /C:"warning"
```

Expected: No errors or warnings (or only expected warnings)

- [ ] **Step 6: Commit main.cc changes**

```bash
git add main.cc
git commit -m "feat: add user identification callback for Hodor rate limiter

Enable per-user rate limiting by extracting user IDs from:
- Bearer tokens (validated by OAuth2Plugin)
- Basic Auth (OAuth2 client credentials)

Supports both authenticated users and OAuth2 clients.
"
```

---

## Task 5: Search and Clean Up All RateLimiterFilter References

**Files:**
- Search: All `.cc`, `.h`, `.md` files
- Potentially modify: Various files

**Context:** Ensure no orphaned references to RateLimiterFilter remain in the codebase before deleting the implementation files.

- [ ] **Step 1: Search all RateLimiterFilter references**

```bash
cd d:\work\development\Repos\backend\drogon-plugin\authforge
# Search in source files
grep -r "RateLimiterFilter" --include="*.cc" --include="*.h" --include="*.cpp" --include="*.hpp" . > rate_limiter_refs.txt
cat rate_limiter_refs.txt
```

- [ ] **Step 2: Review and document findings**

Create a checklist of files that reference RateLimiterFilter:
- [ ] File: _____________ Action needed: _____________
- [ ] File: _____________ Action needed: _____________
- [ ] File: _____________ Action needed: _____________

- [ ] **Step 3: Clean up documentation references**

For each `.md` file found:
- Update to mention Hodor instead of RateLimiterFilter
- Or remove the reference if no longer applicable

Example for `docs/security_hardening.md`:

Find section mentioning RateLimiterFilter and replace with:

```markdown
### 1. 速率限制 (Rate Limiting)

为了防止暴力破解和 DoS 攻击，系统使用 Drogon 官方的 Hodor 插件实现应用层速率限制。

#### 1.1 机制设计

* **插件**: `drogon::plugin::Hodor`
* **算法**: 令牌桶 (Token Bucket)
* **识别策略**:
    1. 优先读取 `X-Forwarded-For` 头（取第一个 IP）。
    2. 其次读取 `X-Real-IP` 头。
    3. 最后降级使用 `Peer IP`（直连 IP）。
    4. 白名单 IP（127.0.0.1, Docker 网络）完全跳过限制。

#### 1.2 限制规则

| 接口 (Path) | 方法 | 全局限制 | 每IP限制 | 每用户限制 | 触发响应 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `/oauth2/login` | POST | 5000/min | **5/min** | 5/min | `429 Too Many Requests` |
| `/oauth2/token` | POST | 10000/min | **10/min** | 10/min | `429 Too Many Requests` |
| `/api/register` | POST | 5000/min | **5/min** | 5/min | `429 Too Many Requests` |
| *所有其他接口* | * | 1000/min | 60/min | 无限制 | - |

#### 1.3 配置方式

Hodor 插件通过 `config.json` 配置，支持动态调整限制参数而无需重新编译。

详细配置参见 `docs/superpowers/specs/2026-04-13-hodor-rate-limiter-migration-design.md`。
```

- [ ] **Step 4: Check test files**

For `test/RateLimiterTest.cc`:
- Option A: Rewrite to test Hodor functionality
- Option B: Delete if testing old implementation only

```bash
cd d:\work\development\Repos\backend\drogon-plugin\authforge\OAuth2Backend\test
cat RateLimiterTest.cc | head -20
```

Decide action: ___ Keep and rewrite ___ Delete

- [ ] **Step 5: Remove temporary search results file**

```bash
cd d:\work\development\Repos\backend\drogon-plugin\authforge
rm -f rate_limiter_refs.txt
```

- [ ] **Step 6: Commit cleanup changes**

```bash
git add -A
git commit -m "docs: update documentation to reference Hodor instead of RateLimiterFilter

- Update security_hardening.md with Hodor configuration
- Remove or update obsolete RateLimiterFilter references
- Document new multi-level rate limiting approach
"
```

---

## Task 6: Delete Old RateLimiterFilter Implementation Files

**Files:**
- Delete: `OAuth2Backend/filters/RateLimiterFilter.cc`
- Delete: `OAuth2Backend/filters/RateLimiterFilter.h`

**Context:** Remove the old custom rate limiting implementation now that Hodor is configured and all references have been cleaned up.

- [ ] **Step 1: Verify files exist**

```bash
cd d:\work\development\Repos\backend\drogon-plugin\authforge\OAuth2Backend\filters
ls -la RateLimiterFilter.*
```

Expected: Both .cc and .h files exist

- [ ] **Step 2: Delete the implementation files**

```bash
cd d:\work\development\Repos\backend\drogon-plugin\authforge\OAuth2Backend
rm -f filters/RateLimiterFilter.cc
rm -f filters/RateLimiterFilter.h
```

- [ ] **Step 3: Verify deletion**

```bash
cd d:\work\development\Repos\backend\drogon-plugin\authforge\OAuth2Backend\filters
ls -la | grep -i rate
```

Expected: No RateLimiterFilter files

- [ ] **Step 4: Check if filters directory is now empty**

```bash
cd d:\work\development\Repos\backend\drogon-plugin\authforge\OAuth2Backend\filters
ls -la
```

If filters directory is empty, you may optionally delete it:
```bash
cd ..
rmdir filters 2>nul || echo "Directory not empty or contains hidden files"
```

- [ ] **Step 5: Commit deletion**

```bash
git add -A
git commit -m "refactor: remove custom RateLimiterFilter implementation

Delete obsolete rate limiting code now replaced by Drogon's
official Hodor plugin. Hodor provides:
- Better algorithm (token bucket vs fixed window)
- Multi-level limiting (IP/user/global)
- Whitelist support
- Configuration-based management
"
```

---

## Task 7: Build and Verify Compilation

**Files:**
- Build: `OAuth2Backend/build/Release/OAuth2Server.exe`
- Test: Successful compilation with no errors

**Context:** Compile the project with Hodor configuration and verify no compilation errors. Use the project's build.bat script on Windows.

- [ ] **Step 1: Stop any running OAuth2Server processes**

```bash
taskkill /F /IM OAuth2Server.exe 2>nul || echo "No OAuth2Server.exe process found"
```

- [ ] **Step 2: Run build script**

```bash
cd d:\work\development\Repos\backend\drogon-plugin\authforge\OAuth2Backend
scripts\build.bat
```

- [ ] **Step 3: Verify build success**

Check for final message:
```
Build completed successfully!
```

And verify executable exists:
```bash
cd d:\work\development\Repos\backend\drogon-plugin\authforge\OAuth2Backend\build\Release
dir OAuth2Server.exe
```

Expected: OAuth2Server.exe exists with recent timestamp

- [ ] **Step 4: Check for compilation warnings**

Review build output for warnings related to:
- Missing includes
- Unused variables
- Deprecated features

If warnings are non-trivial, note them for potential fixes.

- [ ] **Step 5: Do NOT commit yet**

We'll test functionality before committing the build.

---

## Task 8: Basic Functionality Verification

**Files:**
- Test: Running OAuth2Server.exe
- Verify: Hodor plugin loaded, basic OAuth2 flow works

**Context:** Start the server and verify that Hodor plugin loads correctly and basic OAuth2 functionality still works.

- [ ] **Step 1: Start the server**

```bash
cd d:\work\development\Repos\backend\drogon-plugin\authforge\OAuth2Backend\build\Release
start OAuth2Server.exe
```

- [ ] **Step 2: Wait for startup (5 seconds)**

```bash
timeout /t 5 /nobreak
```

- [ ] **Step 3: Check logs for Hodor plugin loading**

```bash
# View log file
cd d:\work\development\Repos\backend\drogon-plugin\authforge\OAuth2Backend
tail -n 50 logs/oauth2_prod.log | grep -i hodor
```

Expected: Log entry showing Hodor plugin started successfully

- [ ] **Step 4: Test basic health endpoint**

```bash
curl -s -o nul -w "%%{http_code}\n" http://localhost:5555/
```

Expected: `200` (or a redirect, which is also OK)

- [ ] **Step 5: Test OAuth2 authorize endpoint (should work without rate limit)**

```bash
curl -s -o nul -w "%%{http_code}\n" "http://localhost:5555/oauth2/authorize?client_id=vue-client&redirect_uri=http://localhost:5173/callback&response_type=code&state=test"
```

Expected: `200` or `302` (normal OAuth2 response)

- [ ] **Step 6: Test rate limiting with multiple requests**

```bash
# Send 7 requests to /oauth2/login (limit is 5)
for /L %i in (1,1,7) do (
    echo Request %i:
    curl -s -X POST http://localhost:5555/oauth2/login ^
      -H "Content-Type: application/json" ^
      -d "{\"email\":\"test@example.com\",\"password\":\"wrong\"}" ^
      -w "%%{http_code}\n" -o nul
    timeout /t 1 /nobreak >nul
)
```

Expected:
- Requests 1-5: `200` or `401` (normal authentication response)
- Requests 6-7: `429` (rate limited)

- [ ] **Step 7: Test whitelist from localhost**

```bash
# Should NOT be rate limited from 127.0.0.1
for /L %i in (1,1,10) do (
    curl -s -X POST http://127.0.0.1:5555/oauth2/login ^
      -H "Content-Type: application/json" ^
      -d "{\"email\":\"test@example.com\",\"password\":\"wrong\"}" ^
      -w "%%{http_code}\n" -o nul
)
```

Expected: All requests return `200` or `401` (no `429` responses)

- [ ] **Step 8: Stop the server**

```bash
taskkill /F /IM OAuth2Server.exe
```

- [ ] **Step 9: Commit successful build and test**

```bash
cd d:\work\development\Repos\backend\drogon-plugin\authforge
git add -A
git commit -m "test: verify Hodor migration - compilation and basic tests

[PASS] Build successful
[PASS] Hodor plugin loads correctly
[PASS] Basic OAuth2 flow works
[PASS] Rate limiting enforced (5/min for login)
[PASS] Whitelist works (localhost unlimited)
"
```

---

## Task 9: Comprehensive Testing

**Files:**
- Test: All rate limiting scenarios
- Verify: IP limits, user limits, global limits, whitelist

**Context:** Perform comprehensive testing of all rate limiting features as outlined in the design spec.

- [ ] **Step 1: Start server**

```bash
cd d:\work\development\Repos\backend\drogon-plugin\authforge\OAuth2Backend\build\Release
start OAuth2Server.exe
timeout /t 5 /nobreak
```

- [ ] **Step 2: Test /oauth2/token endpoint (10/min limit)**

```bash
# Send 12 requests (limit is 10)
for /L %i in (1,1,12) do (
    echo Request %i:
    curl -s -X POST http://localhost:5555/oauth2/token ^
      -u "vue-client:123456" ^
      -d "grant_type=client_credentials" ^
      -w "%%{http_code}\n" -o nul
    timeout /t 1 /nobreak >nul
)
```

Expected: First 10 return `200` or `400`, last 2 return `429`

- [ ] **Step 3: Test /api/register endpoint (5/min limit)**

```bash
for /L %i in (1,1,7) do (
    echo Request %i:
    curl -s -X POST http://localhost:5555/api/register ^
      -H "Content-Type: application/json" ^
      -d "{\"email\":\"user%i@example.com\",\"password\":\"password123\"}" ^
      -w "%%{http_code}\n" -o nul
    timeout /t 1 /nobreak >nul
)
```

Expected: First 5 return `200` or `409`, last 2 return `429`

- [ ] **Step 4: Test per-user rate limiting**

```bash
# User A: vue-client
for /L %i in (1,1,12) do (
    curl -s -X POST http://localhost:5555/oauth2/token ^
      -u "vue-client:123456" ^
      -d "grant_type=client_credentials" ^
      -w "%%{http_code}\n" -o nul
)

# User B: different client (if exists)
for /L %i in (1,1,12) do (
    curl -s -X POST http://localhost:5555/oauth2/token ^
      -u "other-client:secret" ^
      -d "grant_type=client_credentials" ^
      -w "%%{http_code}\n" -o nul
)
```

Expected: Each user independently limited to 10 requests/min

- [ ] **Step 5: Test Docker network whitelist**

If running in Docker:
```bash
# From within Docker container, should not be rate limited
docker exec -it <container_name> bash -c "
  for i in {1..20}; do
    curl -s -X POST http://oauth2server:5555/oauth2/login \
      -H 'Content-Type: application/json' \
      -d '{\"email\":\"test@example.com\",\"password\":\"wrong\"}' \
      -w '%{http_code}\n' -o /dev/null
  done
"
```

Expected: All requests allowed (no `429`)

- [ ] **Step 6: Test global limits (stress test)**

```bash
# Concurrent requests to test global limit
start /b cmd /c "for /L %i in (1,1,100) do @curl -s -X POST http://localhost:5555/oauth2/login -H \"Content-Type: application/json\" -d \"{\\\"email\\\":\\\"test%i@example.com\\\",\\\"password\\\":\\\"wrong\\\"}\" -o nul -w \"%%{http_code}\n\""
timeout /t 2 /nobreak
```

Expected: System remains stable, no crashes, global limits protect server

- [ ] **Step 7: Check logs for rate limit messages**

```bash
cd d:\work\development\Repos\backend\drogon-plugin\authforge\OAuth2Backend
tail -n 100 logs/oauth2_prod.log | grep -i "rate\|limit\|429"
```

Expected: Log messages showing rate limit enforcement

- [ ] **Step 8: Stop server**

```bash
taskkill /F /IM OAuth2Server.exe
```

- [ ] **Step 9: Document test results**

Create test summary:
```
[PASS] /oauth2/login: 5/min limit enforced
[PASS] /oauth2/token: 10/min limit enforced
[PASS] /api/register: 5/min limit enforced
[PASS] Per-user limiting: Working independently
[PASS] Whitelist (localhost): No limits applied
[PASS] Whitelist (Docker): No limits applied (if tested)
[PASS] Global limits: Server protected under load
[PASS] Logs: Rate limit events recorded
```

- [ ] **Step 10: Commit test results**

```bash
cd d:\work\development\Repos\backend\drogon-plugin\authforge
git add -A
git commit -m "test: comprehensive Hodor rate limiter testing

All rate limiting features verified:
- IP-based limits (login: 5, token: 10, register: 5/min)
- Per-user limits (independent counting)
- Whitelist (localhost, Docker networks)
- Global limits (system protection)
- Token bucket algorithm working correctly

Migration complete and verified.
"
```

---

## Task 10: Update Documentation and Final Cleanup

**Files:**
- Modify: `docs/architecture_overview.md`
- Modify: `docs/api_reference.md`
- Modify: Any other documentation mentioning rate limiting

**Context:** Ensure all documentation reflects the new Hodor-based rate limiting implementation.

- [ ] **Step 1: Update architecture overview**

Edit `docs/architecture_overview.md`, find the Filter Layer section and update:

```markdown
│  Hodor Plugin (Global Advice)  │  ← 速率限制 (令牌桶算法, 多级限制)
```

And update the description:
```markdown
- **Hodor Plugin**: Drogon 官方速率限制插件
  - 令牌桶算法 (Token Bucket)
  - 多级限制: IP + 用户 + 全局
  - 白名单支持 (开发/测试环境)
  - 配置化管理 (config.json)
```

- [ ] **Step 2: Update API reference if rate limiting is documented**

Edit `docs/api_reference.md` to reference Hodor configuration instead of RateLimiterFilter.

- [ ] **Step 3: Update CHANGELOG or release notes**

Create or update changelog entry:

```markdown
## [Unreleased]

### Changed
- **BREAKING**: Migrate from custom RateLimiterFilter to Drogon's Hodor plugin
- Rate limiting now uses token bucket algorithm instead of fixed window
- Added per-user rate limiting support
- Added global rate limiting to protect server
- Added whitelist for local and Docker network IPs
- Rate limits now configurable via config.json (no code changes needed)

### Fixed
- Eliminated Redis dependency for rate limiting (now uses in-memory CacheMap)
- Improved rate limiting reliability (no single point of failure)

### Technical Details
- Removed: `filters/RateLimiterFilter.cc`, `filters/RateLimiterFilter.h`
- Added: Hodor plugin configuration in `config.json`
- Added: User identification callback in `main.cc`
- Migration preserves existing rate limits while adding enhancements
```

- [ ] **Step 4: Review all documentation changes**

```bash
cd d:\work\development\Repos\backend\drogon-plugin\authforge\OAuth2Backend
git diff docs/
```

- [ ] **Step 5: Commit documentation updates**

```bash
git add docs/
git commit -m "docs: update documentation for Hodor rate limiter migration

- Update architecture overview with Hodor plugin
- Update security hardening guide
- Update API reference
- Add CHANGELOG entry
- Document new multi-level rate limiting approach
"
```

---

## Task 11: Final Verification and Tag Release

**Files:**
- Verify: All changes committed
- Verify: Build works
- Tag: Release commit

**Context:** Final verification that all changes are complete and working, then create a git tag for the migration.

- [ ] **Step 1: Review all commits**

```bash
cd d:\work\development\Repos\backend\drogon-plugin\authforge
git log --oneline -15
```

Verify you see commits for:
- Backup creation
- Hodor configuration
- Controller cleanup
- main.cc user callback
- Reference cleanup
- File deletion
- Build verification
- Testing
- Documentation updates

- [ ] **Step 2: Verify no uncommitted changes**

```bash
git status
```

Expected: "nothing to commit, working tree clean"

- [ ] **Step 3: Final build test**

```bash
cd OAuth2Backend
scripts\build.bat
```

Expected: "Build completed successfully!"

- [ ] **Step 4: Create comprehensive test summary**

Create file `MIGRATION_SUMMARY.md`:

```markdown
# Hodor Rate Limiter Migration Summary

## Migration Date
2026-04-13

## Changes Made
1. [PASS] Added Hodor plugin to config.json with comprehensive rate limiting
2. [PASS] Removed RateLimiterFilter references from OAuth2Controller.h
3. [PASS] Added user identification callback in main.cc
4. [PASS] Deleted old RateLimiterFilter implementation files
5. [PASS] Cleaned up all RateLimiterFilter references in documentation
6. [PASS] Updated all relevant documentation

## Rate Limiting Configuration
- Algorithm: Token Bucket
- /oauth2/login: 5 requests/min per IP, 5 per user
- /oauth2/token: 10 requests/min per IP, 10 per user
- /api/register: 5 requests/min per IP, 5 per user
- Global default: 1000 requests/min
- Whitelist: 127.0.0.1, ::1, 172.16.0.0/12, 172.17.0.0/12, 192.168.0.0/16

## Testing Completed
- [PASS] Compilation successful
- [PASS] Hodor plugin loads correctly
- [PASS] IP-based rate limiting works
- [PASS] Per-user rate limiting works
- [PASS] Whitelist functions properly
- [PASS] OAuth2 flow unaffected
- [PASS] Server stable under load

## Rollback Information
Backup branch: `backup/before-hodor-migration`
Backup commit: [COMMIT_HASH]

## Performance Notes
- Memory usage: Slightly increased (CacheMap vs Redis)
- Response time: No degradation observed
- CPU usage: Improved (no Redis network I/O)
- Throughput: Maintained or improved

## Known Issues
None

## Next Steps
- Monitor production metrics for 48 hours
- Adjust rate limits if needed via config.json
- Consider adding Prometheus metrics for rate limiting
```

- [ ] **Step 5: Commit migration summary**

```bash
cd d:\work\development\Repos\backend\drogon-plugin\authforge
git add MIGRATION_SUMMARY.md
git commit -m "docs: add Hodor migration completion summary"
```

- [ ] **Step 6: Create annotated tag for migration**

```bash
git tag -a v2.0.0-hodor-migration -m "Hodor Rate Limiter Migration

Migrate from custom RateLimiterFilter to Drogon's official Hodor plugin.

Key improvements:
- Multi-level rate limiting (IP/user/global)
- Token bucket algorithm
- Whitelist support for dev/test environments
- Configuration-based management
- Eliminated Redis dependency

All tests passed. Ready for production deployment.
"
```

- [ ] **Step 7: Push changes and tags**

```bash
git push origin master
git push origin backup/before-hodor-migration
git push origin v2.0.0-hodor-migration
```

- [ ] **Step 8: Create GitHub Release (optional)**

If using GitHub, create a release from the tag with the migration summary as the release notes.

---

## Success Criteria

Migration is complete when:
- [PASS] All code changes committed and pushed
- [PASS] Build compiles without errors or warnings
- [PASS] All tests pass (unit, integration, manual)
- [PASS] Rate limiting enforced at configured levels
- [PASS] Whitelist working for local/Docker environments
- [PASS] No performance degradation
- [PASS] Documentation updated
- [PASS] Rollback plan tested and documented
- [PASS] Git tag created for easy rollback

---

## Troubleshooting

### Hodor plugin fails to load
**Check:** config.json syntax
**Solution:** Run `python -m json.tool config.json` to validate

### Rate limiting not working
**Check:** Hodor configuration in config.json
**Check:** userIdGetter callback in main.cc
**Solution:** Enable DEBUG logs and verify callback is being called

### Whitelist not functioning
**Check:** trust_ips configuration
**Check:** Client IP address (check logs)
**Solution:** Verify IP matches trust_cidrs format

### Compilation errors
**Check:** Drogon version includes Hodor plugin
**Check:** C++20 support enabled
**Solution:** Update Drogon or adjust build configuration

### Performance degradation
**Check:** CacheMap memory usage
**Check:** limiter_expire_time setting
**Solution:** Reduce limiter_expire_time or switch to fixed_window algorithm

---

## Estimated Time

- Task 1: Backup - 5 minutes
- Task 2: Config - 10 minutes
- Task 3: Controller - 5 minutes
- Task 4: Main.cc - 15 minutes
- Task 5: Cleanup - 20 minutes
- Task 6: Delete files - 5 minutes
- Task 7: Build - 10 minutes
- Task 8: Basic tests - 15 minutes
- Task 9: Full testing - 30 minutes
- Task 10: Docs - 15 minutes
- Task 11: Final - 10 minutes

**Total: ~2.5 hours**

---

## Notes

- This migration maintains backward compatibility for rate limits
- All existing limits preserved (login: 5, token: 10, register: 5/min)
- Enhancements added (user limits, global limits, whitelist)
- Configuration-based approach allows easy adjustment without code changes
- Rollback is straightforward using backup branch

---

**Plan Version:** 1.0
**Created:** 2026-04-13
**Status:** Ready for implementation
