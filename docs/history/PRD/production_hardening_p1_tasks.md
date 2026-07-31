# P1 Implementation Tasks — 功能完整性

> 对应 `production_hardening_spec.md` 中 P1 章节  
> 前置依赖: P0 全部完成  
> 预计工期: 4-6 周

---

## Task 11: OpenID Connect Core (P1-1)

### 11.1 RSA 密钥管理

**新建文件**:
- `OAuth2Plugin/include/oauth2/JwkManager.h`
- `OAuth2Plugin/src/common/utils/JwkManager.cc`

**功能**:
- 启动时从配置路径加载 RSA 私钥 PEM（或从环境变量 `OAUTH2_SIGNING_KEY`）
- 支持密钥轮转：`active_kid` + `previous_kid`（JWKS 同时暴露两把公钥）
- 提供 `signIdToken(claims) -> std::string` 方法（RS256）

**配置** (`config.json`):
```json
"oidc": {
    "signing_key_path": "/etc/oauth2/private.pem",
    "signing_algorithm": "RS256",
    "id_token_ttl": 3600
}
```

**密钥生成脚本**: `scripts/generate-signing-key.sh`
```bash
openssl genrsa -out private.pem 2048
openssl rsa -in private.pem -pubout -out public.pem
```

### 11.2 id_token 签发

**修改文件**: `OAuth2Plugin/src/services/TokenService.cc`

在 `exchangeCodeForToken` 和 `refreshAccessToken` 的响应中追加 `id_token` 字段:

```cpp
// 当 scope 包含 "openid" 时签发 id_token
if (scopeContains(authCode->scope, "openid")) {
    Json::Value idTokenClaims;
    idTokenClaims["iss"] = issuer_;
    idTokenClaims["sub"] = authCode->userId;  // public_sub UUID
    idTokenClaims["aud"] = authCode->clientId;
    idTokenClaims["iat"] = now;
    idTokenClaims["exp"] = now + idTokenTtl_;
    if (!nonce.empty()) idTokenClaims["nonce"] = nonce;
    // 可选: name, email, email_verified, picture
    json["id_token"] = jwkManager_->signIdToken(idTokenClaims);
}
```

**依赖库**: 使用 OpenSSL RSA 签名 + base64url 编码手动构造 JWT（避免引入 jwt-cpp 依赖）。
或引入 `jwt-cpp` 作为可选依赖。

### 11.3 OIDC Discovery 端点

**修改文件**: `OAuth2Plugin/src/controllers/OAuth2StandardController.cc`

新增路由:
```cpp
ADD_METHOD_TO(OAuth2StandardController::oidcDiscovery,
    "/.well-known/openid-configuration", drogon::Get);
```

返回标准 OIDC Discovery JSON:
```json
{
    "issuer": "https://auth.example.com",
    "authorization_endpoint": "https://auth.example.com/oauth2/authorize",
    "token_endpoint": "https://auth.example.com/oauth2/token",
    "userinfo_endpoint": "https://auth.example.com/oauth2/userinfo",
    "jwks_uri": "https://auth.example.com/.well-known/jwks.json",
    "introspection_endpoint": "https://auth.example.com/oauth2/introspect",
    "revocation_endpoint": "https://auth.example.com/oauth2/revoke",
    "scopes_supported": ["openid", "profile", "email", "admin"],
    "response_types_supported": ["code"],
    "grant_types_supported": ["authorization_code", "refresh_token", "client_credentials"],
    "subject_types_supported": ["public"],
    "id_token_signing_alg_values_supported": ["RS256"],
    "token_endpoint_auth_methods_supported": ["client_secret_basic", "client_secret_post"],
    "code_challenge_methods_supported": ["S256", "plain"],
    "claims_supported": ["sub", "name", "email", "email_verified", "iss", "aud", "exp", "iat"]
}
```

### 11.4 JWKS 端点

新增路由:
```cpp
ADD_METHOD_TO(OAuth2StandardController::jwks, "/.well-known/jwks.json", drogon::Get);
```

返回 RSA 公钥的 JWK 格式:
```json
{
    "keys": [{
        "kty": "RSA",
        "use": "sig",
        "kid": "key-2026-05",
        "alg": "RS256",
        "n": "...",
        "e": "AQAB"
    }]
}
```

### 11.5 nonce 支持

**修改文件**: `OAuth2Plugin/src/services/TokenService.cc`  
**修改文件**: `OAuth2Plugin/include/oauth2/IOAuth2Storage.h` (OAuth2AuthCode 新增 `nonce` 字段)

- `/oauth2/authorize` 接收 `nonce` 参数，存入 auth_code
- `exchangeCodeForToken` 将 nonce 写入 id_token claims

### 11.6 验收

```bash
# 1. 使用 oidc-client-ts 配置 discovery URL
# 2. 完成授权码流程，获取 id_token
# 3. 用 /.well-known/jwks.json 中的公钥验证 id_token 签名
# 4. nonce 匹配验证通过
```

---

## Task 12: Client Credentials Grant (P1-2)

### 12.1 修改 token 端点

**文件**: `OAuth2Plugin/src/controllers/OAuth2StandardController.cc` (token 方法)

```cpp
if (grantType == "client_credentials") {
    // 1. 验证 client_id + client_secret (必须 CONFIDENTIAL)
    // 2. 验证 client_type != PUBLIC
    // 3. 验证请求的 scope 在 client 允许范围内
    // 4. 颁发 access_token (无 refresh_token, 无 id_token)
    plugin->validateClient(clientId, clientSecret, [...](...) {
        plugin->getClient(clientId, [...](auto client) {
            if (client->clientType == ClientType::PUBLIC) {
                return error("unauthorized_client", "...");
            }
            // 生成 token, scope = 请求的 scope ∩ client.allowedScopes
            auto tokenStr = generateSecureToken();
            // ... save and return
        });
    });
}
```

### 12.2 修改 metadata

在 `grant_types_supported` 中追加 `"client_credentials"`。

### 12.3 验收

```bash
curl -X POST http://localhost:5555/oauth2/token \
  -d "grant_type=client_credentials&client_id=backend-svc&client_secret=xxx&scope=read"
# 返回 {"access_token": "...", "token_type": "Bearer", "expires_in": 3600}
# 无 refresh_token
```

---

## Task 13: 密码重置流程 (P1-3)

### 13.1 新建文件

- `OAuth2Server/controllers/PasswordResetController.h` / `.cc`
- `OAuth2Plugin/include/oauth2/EmailService.h` (接口)
- `OAuth2Server/SmtpEmailService.cc` (SMTP 实现)

### 13.2 Schema

```sql
-- V008__password_reset_tokens.sql
CREATE TABLE IF NOT EXISTS password_reset_tokens (
    token_hash VARCHAR(64) PRIMARY KEY,
    user_id INTEGER NOT NULL REFERENCES users(id),
    expires_at BIGINT NOT NULL,
    used BOOLEAN DEFAULT FALSE,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
```

### 13.3 端点

| 方法 | 路径 | 描述 |
|------|------|------|
| POST | `/api/password-reset/request` | 接收 email，生成 token，发邮件 |
| POST | `/api/password-reset/confirm` | 接收 token + new_password，重置 |

### 13.4 核心逻辑

```cpp
// request:
// 1. 查找 email 对应用户
// 2. 生成 secure token (32 bytes)
// 3. 存 hash 到 password_reset_tokens (TTL 15 min)
// 4. 发送邮件 (含原始 token 链接)
// 5. 无论用户是否存在都返回 200 (防枚举)

// confirm:
// 1. hash(token) 查 DB
// 2. 检查 used=false, expires_at > now
// 3. 更新密码 (Argon2id)
// 4. 标记 token used=true
// 5. 撤销该用户所有 access_token + refresh_token
```

### 13.5 验收

```bash
# 1. POST /api/password-reset/request → 200 (邮件发出)
# 2. 用过期 token confirm → 400
# 3. 用有效 token confirm → 200, 旧 token 全部失效
# 4. 重复使用同一 token → 400
```

---

## Task 14: 邮箱验证 (P1-4)

### 14.1 Schema

```sql
-- V009__email_verification.sql
ALTER TABLE users ADD COLUMN IF NOT EXISTS email_verified BOOLEAN DEFAULT FALSE;

CREATE TABLE IF NOT EXISTS email_verification_tokens (
    token_hash VARCHAR(64) PRIMARY KEY,
    user_id INTEGER NOT NULL REFERENCES users(id),
    email VARCHAR(100) NOT NULL,
    expires_at BIGINT NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
```

### 14.2 修改注册流程

**文件**: `OAuth2Server/controllers/OAuth2Controller.cc` (registerUser)

注册成功后:
1. 生成验证 token
2. 发送验证邮件
3. 返回 "请检查邮箱" 提示

### 14.3 验证端点

```cpp
// GET /api/verify-email?token=xxx
// 1. hash(token) 查 DB
// 2. 更新 users.email_verified = true
// 3. 删除 token 记录
```

### 14.4 登录拦截

**文件**: `OAuth2Server/controllers/OAuth2Controller.cc` (login)

```cpp
// 在 validateUser 成功后、生成 auth code 前:
if (config.require_email_verification && !user.email_verified) {
    return error(401, "email_not_verified", "Please verify your email");
}
```

### 14.5 验收

```bash
# 1. 注册 → 收到验证邮件
# 2. 未验证时登录 → 401 email_not_verified
# 3. 点击验证链接 → email_verified=true
# 4. 再次登录 → 成功
```

---

## Task 15: MFA - TOTP (P1-5)

### 15.1 依赖

使用 libsodium 或 OpenSSL HMAC-SHA1 实现 TOTP (RFC 6238)。

### 15.2 Schema

```sql
-- V010__mfa_support.sql
ALTER TABLE users ADD COLUMN IF NOT EXISTS mfa_enabled BOOLEAN DEFAULT FALSE;
ALTER TABLE users ADD COLUMN IF NOT EXISTS mfa_secret VARCHAR(64);
ALTER TABLE users ADD COLUMN IF NOT EXISTS mfa_backup_codes TEXT;  -- JSON array
```

### 15.3 新建文件

- `OAuth2Server/controllers/MfaController.h` / `.cc`
- `OAuth2Plugin/src/common/utils/TotpUtils.h` / `.cc`

### 15.4 端点

| 方法 | 路径 | 描述 |
|------|------|------|
| POST | `/api/me/mfa/setup` | 生成 secret + QR URI，返回给前端 |
| POST | `/api/me/mfa/verify` | 验证 TOTP code，确认启用 |
| POST | `/api/me/mfa/disable` | 需要密码确认，禁用 MFA |
| POST | `/oauth2/mfa/verify` | 登录第二步，验证 TOTP |

### 15.5 登录流程改造

**文件**: `OAuth2Server/controllers/OAuth2Controller.cc` (login)

```cpp
// 密码验证通过后:
if (user.mfa_enabled) {
    // 不直接颁发 auth code
    // 生成临时 mfa_session_token (短 TTL, 5 min)
    // 返回 {"mfa_required": true, "mfa_token": "..."}
    // 前端跳转到 MFA 输入页
}
// POST /oauth2/mfa/verify:
// 1. 验证 mfa_token 有效
// 2. 验证 TOTP code
// 3. 颁发 auth code
```

### 15.6 备份码

- 启用 MFA 时生成 10 个一次性备份码 (8 字符随机)
- 存储为 hash 数组
- 使用后标记已用

### 15.7 验收

```bash
# 1. POST /api/me/mfa/setup → 返回 otpauth:// URI
# 2. 用 Google Authenticator 扫码
# 3. POST /api/me/mfa/verify (code=123456) → 启用成功
# 4. 登录 → 返回 mfa_required
# 5. POST /oauth2/mfa/verify (正确 code) → 颁发 auth code
# 6. 错误 code → 401
# 7. 备份码可用且一次性
```

---

## Task 16: 结构化审计日志 (P1-6)

### 16.1 Schema

```sql
-- V011__audit_logs.sql
CREATE TABLE IF NOT EXISTS audit_logs (
    id BIGSERIAL PRIMARY KEY,
    timestamp TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP,
    actor_type VARCHAR(20) NOT NULL,  -- 'user', 'client', 'system'
    actor_id VARCHAR(128),
    action VARCHAR(50) NOT NULL,
    target_type VARCHAR(30),
    target_id VARCHAR(128),
    outcome VARCHAR(10) NOT NULL,  -- 'success', 'failure'
    ip VARCHAR(45),
    user_agent TEXT,
    request_id VARCHAR(64),
    details JSONB,
    INDEX idx_audit_timestamp (timestamp),
    INDEX idx_audit_actor (actor_type, actor_id),
    INDEX idx_audit_action (action)
);
```

### 16.2 新建文件

- `OAuth2Plugin/include/oauth2/AuditLogger.h`
- `OAuth2Plugin/src/common/AuditLogger.cc`

```cpp
namespace oauth2 {
class AuditLogger {
public:
    static void log(const AuditEvent& event);  // 异步写入
    struct AuditEvent {
        std::string actorType, actorId, action;
        std::string targetType, targetId, outcome;
        std::string ip, userAgent, requestId;
        Json::Value details;
    };
};
}
```

### 16.3 埋点位置

| 事件 | 文件 | 位置 |
|------|------|------|
| login_success | OAuth2Controller.cc | login 成功回调 |
| login_failure | OAuth2Controller.cc | 密码错误分支 |
| token_issued | TokenService.cc | exchangeCodeForToken 成功 |
| token_refreshed | TokenService.cc | refreshAccessToken 成功 |
| token_revoked | OAuth2StandardController.cc | revoke 成功 |
| refresh_token_reuse | TokenService.cc | reuse 检测分支 |
| password_changed | PasswordResetController.cc | confirm 成功 |
| mfa_enabled | MfaController.cc | verify 成功 |
| client_created | AdminController.cc | 新增 client |
| consent_granted | OAuth2Controller.cc | consent approve |
| consent_revoked | UserSelfServiceController.cc | 撤销授权 |

### 16.4 清理任务

在 `OAuth2CleanupService` 中追加:
```cpp
// 删除超过保留期的审计日志
DELETE FROM audit_logs WHERE timestamp < NOW() - INTERVAL '90 days';
```

### 16.5 验收

```bash
# 1. 登录后查 audit_logs → 有 login_success 记录
# 2. 错误密码后查 → 有 login_failure 记录
# 3. 记录包含 ip, user_agent, request_id
```

---

## Task 17: Admin REST API (P1-7)

### 17.1 新建文件

- `OAuth2Server/controllers/AdminClientController.h` / `.cc`
- `OAuth2Server/controllers/AdminUserController.h` / `.cc`
- `OAuth2Server/controllers/AdminScopeController.h` / `.cc`
- `OAuth2Server/controllers/AdminRoleController.h` / `.cc`

### 17.2 端点清单

**Client 管理**:
| 方法 | 路径 | 描述 |
|------|------|------|
| GET | `/api/admin/clients` | 列表 (分页) |
| POST | `/api/admin/clients` | 创建 |
| GET | `/api/admin/clients/:id` | 详情 |
| PUT | `/api/admin/clients/:id` | 更新 |
| DELETE | `/api/admin/clients/:id` | 删除 |
| POST | `/api/admin/clients/:id/reset-secret` | 重置 secret |

**User 管理**:
| 方法 | 路径 | 描述 |
|------|------|------|
| GET | `/api/admin/users` | 列表 (分页 + 搜索) |
| GET | `/api/admin/users/:id` | 详情 |
| PUT | `/api/admin/users/:id/disable` | 禁用 |
| PUT | `/api/admin/users/:id/enable` | 启用 |
| POST | `/api/admin/users/:id/reset-password` | 管理员重置密码 |
| PUT | `/api/admin/users/:id/roles` | 分配角色 |

**Scope 管理**:
| 方法 | 路径 | 描述 |
|------|------|------|
| GET | `/api/admin/scopes` | 列表 |
| POST | `/api/admin/scopes` | 创建 |
| PUT | `/api/admin/scopes/:name` | 更新 |
| DELETE | `/api/admin/scopes/:name` | 删除 |

**Role/Permission 管理**:
| 方法 | 路径 | 描述 |
|------|------|------|
| GET | `/api/admin/roles` | 列表 |
| POST | `/api/admin/roles` | 创建 |
| PUT | `/api/admin/roles/:id/permissions` | 分配权限 |

### 17.3 权限控制

所有 `/api/admin/*` 端点挂载 `AuthorizationFilter`，配置:
```json
"rbac_rules": {
    "/api/admin/.*": ["admin"]
}
```

### 17.4 验收

```bash
# 1. admin 用户可 CRUD client
# 2. 普通用户访问 /api/admin/* → 403
# 3. 每个操作在 audit_logs 中有记录
# 4. OpenAPI 文档覆盖所有端点
```

---

## Task 18: 用户自服务 (P1-8)

### 18.1 新建文件

- `OAuth2Server/controllers/UserSelfServiceController.h` / `.cc`

### 18.2 端点

| 方法 | 路径 | Filter | 描述 |
|------|------|--------|------|
| GET | `/api/me` | OAuth2Middleware | 当前用户信息 |
| PUT | `/api/me/password` | OAuth2Middleware | 修改密码 |
| GET | `/api/me/sessions` | OAuth2Middleware | 活跃会话列表 |
| DELETE | `/api/me/sessions/:id` | OAuth2Middleware | 终止会话 |
| GET | `/api/me/authorized-apps` | OAuth2Middleware | 已授权应用 |
| DELETE | `/api/me/authorized-apps/:clientId` | OAuth2Middleware | 撤销授权 |
| DELETE | `/api/me` | OAuth2Middleware | 注销账号 |

### 18.3 核心逻辑

**修改密码**:
```cpp
// 1. 验证旧密码
// 2. 用 Argon2id hash 新密码
// 3. 更新 DB
// 4. 撤销该用户所有 token (除当前)
// 5. 审计日志
```

**撤销授权**:
```cpp
// 1. 删除 oauth2_user_consents WHERE user_id=$1 AND client_id=$2
// 2. 撤销该 client 为该用户颁发的所有 token
// 3. 审计日志
```

**注销账号**:
```cpp
// 1. 软删除: users.deleted_at = NOW()
// 2. 撤销所有 token
// 3. 30 天后 cleanup 硬删
```

### 18.4 验收

```bash
# 1. PUT /api/me/password → 旧 token 失效
# 2. DELETE /api/me/authorized-apps/vue-client → 该 client token 失效
# 3. DELETE /api/me → 无法再登录
```

---

## Task 19: PKCE 对 PUBLIC Client 强制 (P1-9)

### 19.1 修改 authorize 端点

**文件**: `OAuth2Plugin/src/controllers/OAuth2StandardController.cc` (authorize)

在 client 验证通过后、scope 验证前:
```cpp
if (client->clientType == ClientType::PUBLIC) {
    std::string codeChallenge = req->getParameter("code_challenge");
    if (codeChallenge.empty()) {
        // 返回 400 invalid_request
        Json::Value err;
        err["error"] = "invalid_request";
        err["error_description"] = "PKCE code_challenge is required for public clients";
        auto resp = HttpResponse::newHttpJsonResponse(err);
        resp->setStatusCode(k400BadRequest);
        callback(resp);
        return;
    }
    std::string method = req->getParameter("code_challenge_method");
    if (method.empty()) method = "plain";
    if (method != "S256" && method != "plain") {
        // 返回 400
    }
}
```

### 19.2 修改 metadata

从 `token_endpoint_auth_methods_supported` 中移除 `"none"`（introspect/revoke 端点同理）。

### 19.3 验收

```bash
# PUBLIC client 不带 code_challenge → 400
# PUBLIC client 带 code_challenge=xxx&code_challenge_method=S256 → 正常
# CONFIDENTIAL client 不带 code_challenge → 正常
```

---

## Task 20: CachedStorage 写后失效 (P1-10)

### 20.1 修改 CachedOAuth2Storage

**文件**: `OAuth2Plugin/src/storage/CachedOAuth2Storage.cc`

在所有写操作完成后 evict 缓存:

```cpp
void CachedOAuth2Storage::revokeAccessToken(
    const std::string& token, const std::string& revokedBy, VoidCallback&& cb) {
    impl_->revokeAccessToken(token, revokedBy, [this, token, cb = std::move(cb)]() {
        // Evict L1
        tokenCache_.erase(token);
        // Evict L2 (Redis)
        redisClient_->execCommandAsync(
            [](auto&&...) {},
            [](auto&&...) {},
            "DEL oauth2:token:%s", token.c_str()
        );
        // Publish invalidation event (multi-instance)
        redisClient_->execCommandAsync(
            [](auto&&...) {},
            [](auto&&...) {},
            "PUBLISH oauth2:invalidate token:%s", token.c_str()
        );
        if (cb) cb();
    });
}
```

### 20.2 订阅失效事件

在 `CachedOAuth2Storage` 构造函数中:
```cpp
// 订阅 Redis channel
redisClient_->subscribe("oauth2:invalidate",
    [this](const std::string& channel, const std::string& message) {
        // 解析 message → evict 对应 L1 缓存
        if (message.find("token:") == 0) {
            tokenCache_.erase(message.substr(6));
        } else if (message.find("client:") == 0) {
            clientCache_.erase(message.substr(7));
        }
    }
);
```

### 20.3 验收

```bash
# 1. 获取 token → 缓存命中
# 2. 撤销 token → 立即失效 (不等 TTL)
# 3. 多实例: 实例 A 撤销 → 实例 B 缓存也失效
```

---

## Task 21: Schema Migration 工具 (P1-11)

### 21.1 新建文件

- `OAuth2Server/SchemaManager.h`
- `OAuth2Server/SchemaManager.cc`

### 21.2 实现

```cpp
class SchemaManager {
public:
    // 扫描 migrations 目录，执行未应用的迁移
    static bool migrate(const drogon::orm::DbClientPtr& db,
                        const std::string& migrationsDir);
    // Dry-run 模式 (只打印不执行)
    static void dryRun(const std::string& migrationsDir);
private:
    static std::vector<MigrationFile> scanMigrations(const std::string& dir);
    static std::set<int> getAppliedVersions(const drogon::orm::DbClientPtr& db);
};
```

### 21.3 集成到 main.cc

**文件**: `OAuth2Server/main.cc`

```cpp
// 在 app().run() 之前:
auto db = drogon::app().getDbClient();
if (!SchemaManager::migrate(db, "./sql/migrations")) {
    LOG_FATAL << "Schema migration failed!";
    return 1;
}
```

### 21.4 验收

```bash
# 1. 空库启动 → 自动建表
# 2. 已有库启动 → 只执行增量
# 3. schema_migrations 表记录正确
# 4. 迁移文件语法错误 → 启动失败，日志清晰
```

---

## 实施顺序建议

```
Week 1-2: Task 11 (OIDC) — 最大工作量
Week 2:   Task 12 (Client Credentials) — 相对简单
Week 3:   Task 13 + 14 (密码重置 + 邮箱验证) — 共享 EmailService
Week 3-4: Task 15 (MFA) — 独立模块
Week 4:   Task 16 (审计日志) — 贯穿所有模块
Week 5:   Task 17 (Admin API) — 依赖审计
Week 5:   Task 18 (用户自服务) — 依赖审计
Week 6:   Task 19 + 20 + 21 (PKCE/缓存/Migration) — 收尾
```

---

## 新增/修改文件汇总

| 操作 | 文件路径 |
|------|----------|
| 新建 | `OAuth2Plugin/include/oauth2/JwkManager.h` |
| 新建 | `OAuth2Plugin/src/common/utils/JwkManager.cc` |
| 新建 | `OAuth2Plugin/include/oauth2/AuditLogger.h` |
| 新建 | `OAuth2Plugin/src/common/AuditLogger.cc` |
| 新建 | `OAuth2Plugin/src/common/utils/TotpUtils.h` / `.cc` |
| 新建 | `OAuth2Server/controllers/PasswordResetController.h` / `.cc` |
| 新建 | `OAuth2Server/controllers/MfaController.h` / `.cc` |
| 新建 | `OAuth2Server/controllers/AdminClientController.h` / `.cc` |
| 新建 | `OAuth2Server/controllers/AdminUserController.h` / `.cc` |
| 新建 | `OAuth2Server/controllers/AdminScopeController.h` / `.cc` |
| 新建 | `OAuth2Server/controllers/AdminRoleController.h` / `.cc` |
| 新建 | `OAuth2Server/controllers/UserSelfServiceController.h` / `.cc` |
| 新建 | `OAuth2Server/SmtpEmailService.cc` |
| 新建 | `OAuth2Plugin/include/oauth2/EmailService.h` |
| 新建 | `OAuth2Server/SchemaManager.h` / `.cc` |
| 新建 | `OAuth2Server/sql/migrations/V008-V011` |
| 新建 | `scripts/generate-signing-key.sh` |
| 修改 | `OAuth2Plugin/src/services/TokenService.cc` |
| 修改 | `OAuth2Plugin/src/controllers/OAuth2StandardController.cc` |
| 修改 | `OAuth2Plugin/include/oauth2/IOAuth2Storage.h` |
| 修改 | `OAuth2Plugin/src/storage/CachedOAuth2Storage.cc` |
| 修改 | `OAuth2Server/controllers/OAuth2Controller.cc` |
| 修改 | `OAuth2Server/main.cc` |
| 修改 | `OAuth2Server/config.json` / `config.prod.json` |
