# Error Handling 合规修复实施计划（v3 — 可执行）

> 审计报告: `.codebuddy/plans/error-handling-audit-report.md`
> 状态: ready | 最后更新: 2026-07-25

---

## 总览

| 阶段 | 违规数 | 涉及文件数 | 预计耗时 |
|------|--------|-----------|---------|
| Phase 1: CRITICAL | 5 | 1 | 30min |
| Phase 2: HIGH | 17 | 5 | 40min |
| Phase 3: MEDIUM | 2 | 2 | 15min |
| Phase 4: LOW | 3 | 1 | 5min |

---

## Phase 1: CRITICAL 修复（5 处）

### 文件: `libs/drogon/src/controllers/OAuth2StandardController.cc`

**问题**: `authorize()` 方法中 5 层嵌套异步回调全部使用 `[this]` 捕获

**修复方案**: 在 `authorize()` 入口处取 `auto self = shared_from_this()`，所有 5 个回调 lambda 中将 `[this, ...]` 替换为 `[self, ...]`

#### 步骤 1.1 — 确认基类已继承 `enable_shared_from_this`

搜索确认:
```cpp
class OAuth2StandardController : public drogon::HttpController<OAuth2StandardController>,
                                  public std::enable_shared_from_this<OAuth2StandardController>
```

若未继承则先添加 `public std::enable_shared_from_this<OAuth2StandardController>`。

#### 步骤 1.2 — 在 `authorize()` 方法入口添加 shared_ptr

在函数体第一行（`auto plugin = ...` 之前或之后）添加:
```cpp
auto self = shared_from_this();
```

#### 步骤 1.3 — 替换 5 个回调的 `[this, ` 为 `[self, `

| 行号范围 | 当前写法 | 目标写法 |
|----------|---------|---------|
| ~923 | `[this, plugin, clientId, redirectUri, scope, state, responseType, req, callback = std::move(callback)]` | `[self, plugin, clientId, redirectUri, scope, state, responseType, req, callback = std::move(callback)]` |
| ~957 | `[this, plugin, clientId, redirectUri, scope, state, responseType, req, callback = std::move(callback)]` | `[self, plugin, clientId, redirectUri, scope, state, responseType, req, callback = std::move(callback)]` |
| ~989 | `[this, plugin, clientId, redirectUri, scope, state, responseType, req, requestedScopes, callback = ...]` | `[self, plugin, clientId, redirectUri, scope, state, responseType, req, requestedScopes, callback = ...]` |
| ~1024 | `[this, plugin, userId, requestedScopes, clientId, scope, redirectUri, state, callback = ...]` | `[self, plugin, userId, requestedScopes, clientId, scope, redirectUri, state, callback = ...]` |
| ~1055 | `[this, plugin, userId, clientId, scope, redirectUri, state, requestedScopes, callback = ...]` | `[self, plugin, userId, clientId, scope, redirectUri, state, requestedScopes, callback = ...]` |

#### 步骤 1.4 — 替换回调内部的 `this->` 为 `self->`

所有 5 个回调内调用 `this->checkUserConsentAndProceed(...)` 的地方改为:
```cpp
self->checkUserConsentAndProceed(...)
```

#### 验证方法
```bash
./manage.sh build-backend
```
编译通过且无新增 warning。

---

## Phase 2: HIGH 修复（17 处）

### 2A. 文件 `libs/drogon/src/AuthService.cc`（3 处空 catch + 1 处 `catch (...)` 修正）

#### 步骤 2A.1 — validateUser: 空 DrogonDbException 处理器（行 88）

当前:
```cpp
[](const ::drogon::orm::DrogonDbException &) {}
```

改为:
```cpp
[](const ::drogon::orm::DrogonDbException &e) {
    LOG_WARN << "Failed to reset failed attempts counter: " << e.base().what();
}
```

#### 步骤 2A.2 — validateUser: 空 DrogonDbException 处理器（行 168）

当前:
```cpp
[](const ::drogon::orm::DrogonDbException &) {}
```

改为:
```cpp
[](const ::drogon::orm::DrogonDbException &e) {
    LOG_WARN << "Failed to update failed attempts: " << e.base().what();
}
```

#### 步骤 2A.3 — getUserInfo: 空 DrogonDbException 处理器（行 365）

当前:
```cpp
[](const ::drogon::orm::DrogonDbException &) {}
```

改为:
```cpp
[](const ::drogon::orm::DrogonDbException &e) {
    LOG_DEBUG << "User " << user->getValueOfId() << " has no roles";
}
```

#### 步骤 2A.4 — Reset 空 catch（如有 `catch (...) {}` 模式）

在 `resetPassword` 或 `changePassword` 方法中，如果存在:
```cpp
catch (...) {
    LOG_ERROR << "sendPasswordResetNotification Exception";
    // 继续，不影响主流程
}
```

确保已包含 `LOG_ERROR`（审计中未发现该文件有空 `catch (...)` 无日志的情况，二次确认即可）。

### 2B. 文件 `libs/drogon/src/controllers/OAuth2StandardController.cc`（1 处空 catch）

#### 步骤 2B.1 — device code deleteBy 空 DrogonDbException 处理器（行 1647）

当前:
```cpp
Mapper<drogon_model::oauth2_db::Oauth2DeviceCodes>(dbClient).deleteBy(
    Criteria(...),
    [](const size_t) {},
    [](const ::drogon::orm::DrogonDbException &) {}  // <-- 空的
);
```

改为:
```cpp
Mapper<drogon_model::oauth2_db::Oauth2DeviceCodes>(dbClient).deleteBy(
    Criteria(...),
    [](const size_t) {},
    [](const ::drogon::orm::DrogonDbException &e) {
        LOG_WARN << "Failed to delete consumed device code: " << e.base().what();
    }
);
```

### 2C. 文件 `libs/drogon/src/controllers/WebAuthnController.cc`（1 处空 catch）

#### 步骤 2C.1 — sign_count update 空 DrogonDbException 处理器（行 563）

当前:
```cpp
Mapper<drogon_model::oauth2_db::WebauthnCredentials>(db).update(
    *credUpdate,
    [](const size_t) {},
    [](const ::drogon::orm::DrogonDbException &) {}  // <-- 空的
);
```

改为:
```cpp
Mapper<drogon_model::oauth2_db::WebauthnCredentials>(db).update(
    *credUpdate,
    [](const size_t) {},
    [](const ::drogon::orm::DrogonDbException &e) {
        LOG_WARN << "Failed to update sign count: " << e.base().what();
    }
);
```

### 2D. 文件 `OAuth2Plugin/src/storage/PostgresTokenRepository.cc`（4 处 `catch (...)` → 具体类型）

#### 步骤 2D.1 — saveAccessToken 外层 catch（行 59）

当前:
```cpp
catch (...) {
    LOG_ERROR << "saveAccessToken Exception";
    if (*sharedCb) (*sharedCb)();
}
```

改为:
```cpp
catch (const DrogonDbException &e) {
    LOG_ERROR << "saveAccessToken Exception: " << e.base().what();
    if (*sharedCb) (*sharedCb)();
}
```

#### 步骤 2D.2 — saveRefreshToken 外层 catch（行 264）

当前:
```cpp
catch (...) {
    LOG_ERROR << "saveRefreshToken Exception";
    ...
}
```

改为:
```cpp
catch (const DrogonDbException &e) {
    LOG_ERROR << "saveRefreshToken Exception: " << e.base().what();
    ...
}
```

#### 步骤 2D.3 — getRefreshToken 外层 catch（行 303）

当前:
```cpp
catch (...) {
    LOG_ERROR << "getRefreshToken Exception";
    if (*sharedCb) (*sharedCb)(ResultType());
}
```

改为:
```cpp
catch (const DrogonDbException &e) {
    LOG_ERROR << "getRefreshToken Exception: " << e.base().what();
    if (*sharedCb) (*sharedCb)(ResultType());
}
```

#### 步骤 2D.4 — revokeRefreshToken 外层 catch（行 343）

当前:
```cpp
catch (...)
```

改为:
```cpp
catch (const DrogonDbException &e)
```

注意: 需确认该 catch 块体中的日志也加上 `e.base().what()`。

### 2E. 文件 `OAuth2Plugin/src/storage/RedisTokenRepository.cc`（1 处类型不匹配）

#### 步骤 2E.1 — execCommandAsync 错误回调（行 181）

当前:
```cpp
[cb](const std::exception &e) {
    LOG_ERROR << "Failed to revoke refresh token in Redis: " << e.what();
    if (cb) cb();
},
```

改为:
```cpp
[cb](const RedisException &e) {
    LOG_ERROR << "Failed to revoke refresh token in Redis: " << e.what();
    if (cb) cb();
},
```

### 2F. 文件 `libs/oauth2/src/protocol/AuthorizationService.cc`（1 处缺 try-catch）

#### 步骤 2F.1 — hasUserConsent 回调添加 try-catch（行 150-173）

当前回调结构:
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

改为包裹 try-catch:
```cpp
[consentMap, remaining, scope, client, hasAdminRole, requestedScopes, callback](bool hasConsent) mutable {
    try {
        (*consentMap)[scope] = hasConsent;
        if (--(*remaining) == 0) {
            callback(authforge::oauth2::access::evaluateScopes(
                requestedScopes, *client, hasAdminRole,
                [consentMap](const std::string &s) { ... }));
        }
    } catch (const std::exception &e) {
        LOG_ERROR << "Scope evaluation failed: " << e.what();
        callback(authforge::oauth2::access::ScopeResult::error("Scope evaluation failed"));
    }
}
```

---

## Phase 3: MEDIUM 修复（2 处）

### 3A. 文件 `libs/drogon/src/filters/AuthorizationFilter.cc`（1 处 `[this]`）

#### 步骤 3A.1 — doFilter validateAccessToken 回调（行 130）

在 `AuthorizationFilter` 类头部确认继承:
```cpp
class AuthorizationFilter : public drogon::HttpFilter<AuthorizationFilter>,
                             public std::enable_shared_from_this<AuthorizationFilter>
```

在 `doFilter()` 方法开头添加:
```cpp
auto self = shared_from_this();
```

将:
```cpp
[this, req, denyCbPtr, nextCbPtr, plugin]
```

改为:
```cpp
[self, req, denyCbPtr, nextCbPtr, plugin]
```

同步修改回调体内所有 `this->` 调用为 `self->`。

### 3B. 文件 `OAuth2Plugin/src/filters/AuthorizationFilter.cc`（1 处 `[this]`）

#### 步骤 3B.1 — doFilter validateAccessToken 回调（行 137）

与 3A 相同的改造方式。

---

## Phase 4: LOW 修复（3 处）

### 4A. 文件 `OAuth2Plugin/src/storage/PostgresTokenRepository.cc`

#### 步骤 4A.1 — purgeExpired 外层 catch（行 724）

当前:
```cpp
catch (...) {
    LOG_ERROR << "PostgresTokenRepository::purgeExpired Exception";
}
```

改为:
```cpp
catch (const DrogonDbException &e) {
    LOG_ERROR << "PostgresTokenRepository::purgeExpired Exception: " << e.base().what();
} catch (const std::exception &e) {
    LOG_ERROR << "PostgresTokenRepository::purgeExpired Exception: " << e.what();
}
```

(定时任务使用 `std::exception` 兜底是合理的，但需明确分层)

---

## 验证检查清单

每个 Phase 完成后执行:

| 步骤 | 命令 | 预期 |
|------|------|------|
| 编译 | `./manage.sh build-backend` | 0 errors, 0 new warnings |
| 单元测试 | `./manage.sh test-backend` | 全部通过 |
| 格式检查 | `clang-format --dry-run -Werror <modified_files>` | 无格式问题 |
| grep 确认 | `grep -rn '\[this\b' libs/drogon/src/controllers/OAuth2StandardController.cc` | 无 `[this` 残留 |
| grep 确认 | `grep -rn 'catch\s*(\s*\.\.\.\s*)' OAuth2Plugin/src/storage/PostgresTokenRepository.cc` | 仅 purgeExpired 允许存在 1 处兜底 |

---

## 不纳入修复的项目（原因说明）

| 项目 | 原因 |
|------|------|
| `models/` 中 `[this]` 捕获 (~20 处) | `drogon_ctl` 自动生成，根据 `orm-models` 规则不手工修改 |
| 测试文件中 `catch (...)` (~30 处) | 测试代码对异常类型无严格要求 |
| `OAuth2Plugin.cc` 中 `[this]` 同步捕获 | `std::call_once` + `initStorage` 在 `initAndStart` 中同步执行，无异步生命周期问题 |
