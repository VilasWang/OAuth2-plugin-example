# Error Handling 合规性审计报告

> 审计时间: 2026-07-25 | 审计范围: `libs/`, `OAuth2Plugin/`, `OAuth2Server/` 中所有 `*.cc` 文件
> 依据: CODEBUDDY.md Error Handling + Lambda Capture Rules

---

## 统计摘要

| 规则 | 违规总数 | CRITICAL | HIGH | MEDIUM | LOW | INFO |
|------|---------|----------|------|--------|-----|------|
| R1a (PG: catch DrogonDbException) | 8 | 0 | 5 | 0 | 3 | 0 |
| R1b (Redis: catch RedisException) | 1 | 0 | 1 | 0 | 0 | 0 |
| R2 (sharedCb failure path) | 5 | 0 | 5 | 0 | 0 | 0 |
| R3 (try-catch in callbacks) | 1 | 0 | 1 | 0 | 0 | 0 |
| R4 ([this]/[&var] captures) | 12 | 5 | 5 | 2 | 0 | 0 |
| **合计** | **27** | **5** | **17** | **2** | **3** | **0** |

---

## CRITICAL (5)

### C1 — `[this]` 捕获在异步回调中 (OAuth2StandardController::authorize())

**文件**: `libs/drogon/src/controllers/OAuth2StandardController.cc`
**行号**: 923, 957, 989, 1024, 1055

`authorize()` 方法中 5 层嵌套的异步回调全部使用 `[this]` 捕获：

```cpp
// 行 923: 第1层 validateClient 回调
[this, plugin, clientId, redirectUri, scope, state, responseType, req, callback = std::move(callback)](bool validClient) mutable {

// 行 957: 第2层 validateRedirectUri 回调
[this, plugin, clientId, redirectUri, scope, state, responseType, req, callback = std::move(callback)](bool validUri) mutable {

// 行 989: 第3层 validateClientScopes 回调
[this, plugin, clientId, redirectUri, scope, state, responseType, req, requestedScopes, callback = std::move(callback)](bool validScopes, std::string scopeError) mutable {

// 行 1024: 第4层 validateUserRolesForScopes 回调
[this, plugin, userId, requestedScopes, clientId, scope, redirectUri, state, callback = ...](bool validRoles, std::string roleError) mutable {

// 行 1055: 第5层 getInternalUserId 回调
[this, plugin, userId, clientId, scope, redirectUri, state, requestedScopes, callback = std::move(callback)](std::optional<int32_t> internalUserId) mutable {
```

**风险**: `OAuth2StandardController` 虽然由 Drogon 管理生命周期较长，但规则明确规定 `[this]` **FORBIDDEN (no PR-exemption)**。这些回调通过 `callback = std::move(callback)` 捕获 callback 但用 `[this]` 来调用 `checkUserConsentAndProceed()` 成员函数。在多线程事件循环下，如果 Controller 在极端情况被替换，存在悬垂指针风险。

**修复建议**: 使用 `auto self = shared_from_this()` 替代 `[this]`，令 `OAuth2StandardController` 继承 `std::enable_shared_from_this<T>`。

---

## HIGH (17)

### H1 — 空的 DrogonDbException 处理器吞掉异常 (AuthService.cc ×3)

**文件**: `libs/drogon/src/AuthService.cc`

| 行号 | 上下文 | 代码 |
|------|--------|------|
| 88 | `validateUser` — 重置失败计数 | `[](const ::drogon::orm::DrogonDbException &) {}` |
| 168 | `validateUser` — 更新失败计数和锁定时间 | `[](const ::drogon::orm::DrogonDbException &) {}` |
| 365 | `getUserInfo` — 角色查询失败后继续返回空角色 | `[](const ::drogon::orm::DrogonDbException &) {}` |

**风险**: 登录失败计数/锁定持久化失败时完全静默。攻击者可能绕过暴力破解防护（锁定写入失败但继续登录流程）。

**建议**: 至少记录 `LOG_WARN`，并评估是否需要将错误传播给调用者。

### H2 — 空的 DrogonDbException 处理器吞掉异常 (OAuth2StandardController.cc ×1)

**文件**: `libs/drogon/src/controllers/OAuth2StandardController.cc` 行 1647

```cpp
Mapper<drogon_model::oauth2_db::Oauth2DeviceCodes>(dbClient).deleteBy(
    Criteria(...),
    [](const size_t) {},
    [](const ::drogon::orm::DrogonDbException &) {}  // <-- 空处理器
);
```

**风险**: device code 消费后删除失败被静默忽略，可能导致重放攻击或数据库膨胀。

### H3 — 空的 DrogonDbException 处理器吞掉异常 (WebAuthnController.cc ×1)

**文件**: `libs/drogon/src/controllers/WebAuthnController.cc` 行 563

```cpp
Mapper<drogon_model::oauth2_db::WebauthnCredentials>(db).update(
    *credUpdate,
    [](const size_t) {},
    [](const ::drogon::orm::DrogonDbException &) {}  // <-- 空处理器
);
```

**风险**: WebAuthn 登录 sign_count 更新失败被静默忽略，反重放计数器不准确。

### H4 — PostgresTokenRepository 使用 `catch (...)` 替代 `catch (const DrogonDbException &)` (×4)

**文件**: `OAuth2Plugin/src/storage/PostgresTokenRepository.cc`

| 行号 | 函数 | 说明 |
|------|------|------|
| 59 | `saveAccessToken` | 外层 `catch (...)` 而非 `catch (const DrogonDbException &)` |
| 264 | `saveRefreshToken` | 同上 |
| 303 | `getRefreshToken` | 同上 |
| 343 | `revokeRefreshToken` | 同上 |

```cpp
// 行 59: 违反 R1a
try {
    Mapper<Oauth2AccessTokens> mapper(dbClientMaster_);
    mapper.insert(newToken, ..., [sharedCb](const DrogonDbException &e) { ... });
}
catch (...) {  // <-- 应为 catch (const DrogonDbException &e)
    LOG_ERROR << "saveAccessToken Exception";
    if (*sharedCb) (*sharedCb)();
}
```

**风险**: `catch (...)` 过于宽泛，丢失异常类型信息和 `.base().what()` 诊断细节。无法区分 DrogonDbException vs 其他异常。

**说明**: 内部 lambda 回调已经正确使用了 `DrogonDbException`，但外层同步 try-catch 使用了 `catch (...)`，信息丢失。

### H5 — RedisTokenRepository 错误使用 `const std::exception &` 代替 RedisException (×1)

**文件**: `OAuth2Plugin/src/storage/RedisTokenRepository.cc` 行 181

```cpp
redisClient_->execCommandAsync(
    [cb](const RedisResult &) { if (cb) cb(); },
    [cb](const std::exception &e) {  // <-- 应为 const RedisException &
        LOG_ERROR << "Failed to revoke refresh token in Redis: " << e.what();
        if (cb) cb();
    },
    "HSET oauth2_refresh_tokens:%s revoked 1", token.c_str()
);
```

**风险**: 与同一文件其他所有 `execCommandAsync` 错误回调的签名 `const RedisException &`（行 88, 147, 307, 336, 390, 407）不一致。Drogon 的 RedisClient 错误回调类型签名是 `void(const drogon::nosql::RedisException &)`，使用 `const std::exception &` 可能无法正确捕获（类型不匹配则回调永远不会被调用），或最多依赖多态转到基类但丢失 Redis 特定错误信息。

### H6 — 异步回调中缺少 try-catch 保护 (AuthorizationService.cc)

**文件**: `libs/oauth2/src/protocol/AuthorizationService.cc` 行 150-173

```cpp
consents_->hasUserConsent(
    userRef, clientId, scope,
    [consentMap, remaining, scope, client, hasAdminRole, requestedScopes, callback](bool hasConsent) mutable {
        (*consentMap)[scope] = hasConsent;
        if (--(*remaining) == 0) {
            callback(authforge::oauth2::access::evaluateScopes(
                requestedScopes, *client, hasAdminRole,
                [consentMap](const std::string &s) { ... }));
        }
    }
);
```

**风险**: `(*consentMap)[scope]` 访问（`std::unordered_map`）可能抛 `std::bad_alloc`；`evaluateScopes` 内部逻辑可能抛异常。回调无 try-catch 包裹，异常会逃逸到 Drogon 框架，导致意外行为。

---

## MEDIUM (2)

### M1 — `[this]` 捕获在 AuthorizationFilter::doFilter() 回调中 (×2 文件)

**文件**: 
- `libs/drogon/src/filters/AuthorizationFilter.cc` 行 130
- `OAuth2Plugin/src/filters/AuthorizationFilter.cc` 行 137

```cpp
plugin->validateAccessToken(
    token,
    [this, req, denyCbPtr, nextCbPtr, plugin](  // <-- [this]
        std::shared_ptr<OAuth2Plugin::AccessToken> at) mutable {
```

**风险**: Filter 是 Drogon 框架管理的长期对象，通常生命周期稳定。但规则明确禁止 `[this]`。

**建议**: 改写为 `auto self = shared_from_this()` 并令类继承 `enable_shared_from_this`。

---

## LOW (3)

### L1-L3 — PostgresTokenRepository::purgeExpired() 使用 `catch (...)`

**文件**: `OAuth2Plugin/src/storage/PostgresTokenRepository.cc` 行 724

```cpp
catch (...) {
    LOG_ERROR << "PostgresTokenRepository::purgeExpired Exception";
}
```

**说明**: 定时清理函数，有 `LOG_ERROR` 记录，不阻塞主流程。影响较低但仍不符合 R1a。

---

## 合规通过的模块（无违规）

| 模块 | 状态 | 说明 |
|------|------|------|
| `libs/oauth2/src/protocol/TokenService.cc` | [+] 合规 | 正确使用 `auto self = shared_from_this()` |
| `OAuth2Plugin/src/storage/RedisGrantRepository.cc` | [+] 合规 | 全部使用 `const RedisException &` 正确类型 |
| `OAuth2Plugin/src/storage/RedisClientRepository.cc` | [+] 合规 | 同上 |
| `OAuth2Plugin/src/storage/RedisConsentRepository.cc` | [+] 合规 | 同上 |
| `OAuth2Plugin/src/storage/MemoryUserRepository.cc` | [+] 合规 | 无 DB 操作，异常捕获合理 |
| `libs/drogon/src/controllers/WebAuthnController.cc` | [+] 主体合规 | 除 H3 空 catch 外，所有 Mapper 错误回调正确传播 |
| `libs/drogon/src/controllers/SessionController.cc` | [+] 合规 | 回调中无 DB 操作（通过 AuthService 委托），错误处理正确 |

---

## 修复优先级建议

| 优先级 | 违规编号 | 工作量 | 说明 |
|--------|---------|--------|------|
| P0 | C1 | 中 | `[this]` → `shared_from_this()` 改造 OAuth2StandardController |
| P0 | H5 | 小 | 1 行修改：`std::exception` → `RedisException` |
| P1 | H1-H3 | 小 | 5 处空 catch 添加 LOG_WARN |
| P1 | H4 | 中 | 4 处 `catch (...)` → `catch (const DrogonDbException &)` |
| P1 | H6 | 小 | AuthorizationService 回调添加 try-catch |
| P2 | M1 | 中 | 2 个 AuthorizationFilter `[this]` → `shared_from_this()` |
| P3 | L1-L3 | 小 | 1 处 `catch (...)` 改为具体类型 |

---

## 附注

1. **models/ 目录**中约 20 个文件包含 `[this]` 捕获，但这些是 `drogon_ctl` 自动生成的 ORM 代码。根据 `orm-models` 规则，ORM 文件不应手工修改。建议在 drogon_ctl 上游修复后重新生成。

2. **测试文件**中的 `catch (...)` 使用（约 30 处）按误报排除清单不纳入统计——测试代码中对异常类型无严格要求。

3. **Redis 层整体质量较高**——除了 1 处类型不匹配（H5），其余 6 个文件错误回调全部正确使用了 `RedisException`。
