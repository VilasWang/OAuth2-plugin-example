# P2 Implementation Tasks — 企业级增强

> 对应 `production_hardening_spec.md` 中 P2 章节  
> 前置依赖: P0 + P1 完成  
> 按需实施，每个 Task 独立可选

---

## Task 22: Dynamic Client Registration (P2-1)

### 22.1 RFC 7591 — 注册端点

**新建文件**:
- `OAuth2Plugin/src/controllers/ClientRegistrationController.h` / `.cc`

**端点**:
| 方法 | 路径 | 描述 |
|------|------|------|
| POST | `/oauth2/register` | 注册新 client |
| GET | `/oauth2/register/:client_id` | 查询 client 配置 |
| PUT | `/oauth2/register/:client_id` | 更新 client 配置 |
| DELETE | `/oauth2/register/:client_id` | 删除 client |

**请求体** (POST):
```json
{
    "client_name": "My App",
    "redirect_uris": ["https://myapp.com/callback"],
    "grant_types": ["authorization_code", "refresh_token"],
    "token_endpoint_auth_method": "client_secret_basic",
    "scope": "openid profile email",
    "contacts": ["admin@myapp.com"],
    "logo_uri": "https://myapp.com/logo.png",
    "tos_uri": "https://myapp.com/tos",
    "policy_uri": "https://myapp.com/privacy"
}
```

**响应**:
```json
{
    "client_id": "generated-uuid",
    "client_secret": "generated-secret",
    "client_id_issued_at": 1716100000,
    "client_secret_expires_at": 0,
    "registration_access_token": "...",
    "registration_client_uri": "https://auth.example.com/oauth2/register/generated-uuid"
}
```

### 22.2 访问控制

- 需要 `initial_access_token`（由 admin 预先颁发）或 admin 角色
- `registration_access_token` 用于后续 GET/PUT/DELETE 操作

### 22.3 Schema 变更

```sql
-- V012__client_metadata.sql
ALTER TABLE oauth2_clients ADD COLUMN IF NOT EXISTS contacts TEXT;
ALTER TABLE oauth2_clients ADD COLUMN IF NOT EXISTS logo_uri VARCHAR(512);
ALTER TABLE oauth2_clients ADD COLUMN IF NOT EXISTS tos_uri VARCHAR(512);
ALTER TABLE oauth2_clients ADD COLUMN IF NOT EXISTS policy_uri VARCHAR(512);
ALTER TABLE oauth2_clients ADD COLUMN IF NOT EXISTS
    token_endpoint_auth_method VARCHAR(30) DEFAULT 'client_secret_basic';
ALTER TABLE oauth2_clients ADD COLUMN IF NOT EXISTS created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP;
ALTER TABLE oauth2_clients ADD COLUMN IF NOT EXISTS updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP;
ALTER TABLE oauth2_clients ADD COLUMN IF NOT EXISTS
    registration_access_token_hash VARCHAR(64);
```

### 22.4 验收

```bash
# 1. POST /oauth2/register (带 initial_access_token) → 201 + client_id/secret
# 2. GET /oauth2/register/:id (带 registration_access_token) → 200
# 3. 无 token 访问 → 401
# 4. metadata 中包含 registration_endpoint
```

---

## Task 23: Device Authorization Grant (P2-2)

### 23.1 RFC 8628 实现

**新建文件**:
- `OAuth2Plugin/src/controllers/DeviceAuthController.h` / `.cc`

**端点**:
| 方法 | 路径 | 描述 |
|------|------|------|
| POST | `/oauth2/device_authorization` | 获取 device_code + user_code |
| POST | `/oauth2/token` (grant_type=device_code) | 轮询获取 token |
| GET | `/oauth2/device` | 用户输入 user_code 的页面 |
| POST | `/oauth2/device/verify` | 用户确认授权 |

### 23.2 Schema

```sql
-- V013__device_codes.sql
CREATE TABLE IF NOT EXISTS oauth2_device_codes (
    device_code_hash VARCHAR(64) PRIMARY KEY,
    user_code VARCHAR(8) NOT NULL UNIQUE,
    client_id VARCHAR(50) NOT NULL REFERENCES oauth2_clients(client_id),
    scope TEXT,
    status VARCHAR(20) DEFAULT 'pending',  -- pending/approved/denied/expired
    user_id VARCHAR(50),
    expires_at BIGINT NOT NULL,
    interval_seconds INTEGER DEFAULT 5,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
```

### 23.3 核心流程

```
Device                    Auth Server              User (Browser)
  |                           |                         |
  | POST /device_authorization|                         |
  |-------------------------->|                         |
  |<--------------------------| device_code, user_code  |
  |                           | verification_uri        |
  |                           |                         |
  | (显示 user_code 给用户)    |                         |
  |                           |                         |
  |                           |    GET /oauth2/device   |
  |                           |<------------------------|
  |                           |    输入 user_code       |
  |                           |<------------------------|
  |                           |    登录 + 授权          |
  |                           |<------------------------|
  |                           |    status=approved      |
  |                           |                         |
  | POST /oauth2/token        |                         |
  | (grant_type=device_code)  |                         |
  |-------------------------->|                         |
  |<--------------------------| access_token            |
```

### 23.4 轮询响应

- `authorization_pending` → 用户尚未操作
- `slow_down` → 轮询太快
- `access_denied` → 用户拒绝
- `expired_token` → device_code 过期

### 23.5 验收

```bash
# 1. POST /oauth2/device_authorization → device_code + user_code
# 2. 用户在浏览器输入 user_code 并授权
# 3. 设备轮询 /oauth2/token → 获得 access_token
# 4. 超时后轮询 → expired_token
```

---

## Task 24: Backchannel Logout (P2-3)

### 24.1 实现

**修改文件**: `OAuth2Plugin/include/oauth2/IOAuth2Storage.h`

```cpp
struct OAuth2Client {
    // ... 现有字段 ...
    std::string backchannelLogoutUri;  // 新增
    bool backchannelLogoutSessionRequired = false;
};
```

### 24.2 Schema

```sql
-- V014__backchannel_logout.sql
ALTER TABLE oauth2_clients ADD COLUMN IF NOT EXISTS
    backchannel_logout_uri VARCHAR(512);
ALTER TABLE oauth2_clients ADD COLUMN IF NOT EXISTS
    backchannel_logout_session_required BOOLEAN DEFAULT FALSE;
```

### 24.3 Logout Token 签发

用户登出时，向所有配置了 `backchannel_logout_uri` 的已授权 client 发送 logout_token:

```cpp
// logout_token 是一个 JWT:
{
    "iss": "https://auth.example.com",
    "sub": "user-uuid",
    "aud": "client-id",
    "iat": 1716100000,
    "jti": "unique-id",
    "events": {
        "http://schemas.openid.net/event/backchannel-logout": {}
    }
}
```

### 24.4 异步通知

```cpp
// 使用 Drogon HttpClient 异步 POST 到每个 client 的 logout URI
for (auto& client : authorizedClients) {
    if (!client.backchannelLogoutUri.empty()) {
        auto httpClient = HttpClient::newHttpClient(client.backchannelLogoutUri);
        auto req = HttpRequest::newHttpRequest();
        req->setMethod(Post);
        req->setBody("logout_token=" + signedLogoutToken);
        httpClient->sendRequest(req, [](auto&&...) {});  // fire-and-forget
    }
}
```

### 24.5 验收

```bash
# 1. 配置 client 的 backchannel_logout_uri
# 2. 用户登出
# 3. client 收到 POST 请求，body 含有效 logout_token
# 4. logout_token 可被 JWKS 验证
```

---

## Task 25: 多租户 (P2-4)

### 25.1 Schema

```sql
-- V015__multi_tenant.sql
CREATE TABLE IF NOT EXISTS organizations (
    id SERIAL PRIMARY KEY,
    slug VARCHAR(50) UNIQUE NOT NULL,
    name VARCHAR(200) NOT NULL,
    logo_uri VARCHAR(512),
    primary_color VARCHAR(7),
    issuer_override VARCHAR(512),  -- 可选: 每租户独立 issuer
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

ALTER TABLE users ADD COLUMN IF NOT EXISTS org_id INTEGER REFERENCES organizations(id);
ALTER TABLE oauth2_clients ADD COLUMN IF NOT EXISTS org_id INTEGER REFERENCES organizations(id);
ALTER TABLE oauth2_scopes ADD COLUMN IF NOT EXISTS org_id INTEGER REFERENCES organizations(id);
```

### 25.2 租户隔离策略

- **数据隔离**: 所有查询追加 `WHERE org_id = $current_org`
- **URL 隔离**: 支持 `https://{slug}.auth.example.com` 或 `https://auth.example.com/{slug}/oauth2/...`
- **品牌隔离**: 登录页 logo/颜色按租户配置
- **Scope 隔离**: 每个租户可定义独立 scope

### 25.3 核心改动

- `IOAuth2Storage` 所有方法追加可选 `orgId` 参数
- `OAuth2Middleware` 从请求中解析当前租户（subdomain / path prefix / header）
- 登录页模板支持动态品牌

### 25.4 验收

```bash
# 1. 创建 org "acme"
# 2. 在 acme 下创建 client
# 3. 访问 /acme/oauth2/authorize → 显示 acme 品牌登录页
# 4. acme 的 client 无法访问其他 org 的用户
```

---

## Task 26: WebAuthn / Passkey (P2-5)

### 26.1 依赖

使用 `libfido2` 或纯 C++ WebAuthn 库。

### 26.2 Schema

```sql
-- V016__webauthn.sql
CREATE TABLE IF NOT EXISTS webauthn_credentials (
    id SERIAL PRIMARY KEY,
    user_id INTEGER NOT NULL REFERENCES users(id),
    credential_id BYTEA NOT NULL UNIQUE,
    public_key BYTEA NOT NULL,
    sign_count INTEGER DEFAULT 0,
    transports TEXT,  -- JSON array
    name VARCHAR(100),
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    last_used_at TIMESTAMP
);
```

### 26.3 端点

| 方法 | 路径 | 描述 |
|------|------|------|
| POST | `/api/me/webauthn/register/begin` | 生成 registration options |
| POST | `/api/me/webauthn/register/finish` | 完成注册 |
| POST | `/oauth2/webauthn/authenticate/begin` | 生成 authentication options |
| POST | `/oauth2/webauthn/authenticate/finish` | 完成认证 |

### 26.4 验收

```bash
# 1. 用户注册 Passkey (浏览器 WebAuthn API)
# 2. 登录时选择 Passkey → 无密码完成认证
# 3. sign_count 递增
```

---

## Task 27: 账号锁定与渐进退避 (P2-6)

### 27.1 Schema

```sql
-- V017__account_lockout.sql
ALTER TABLE users ADD COLUMN IF NOT EXISTS failed_login_count INTEGER DEFAULT 0;
ALTER TABLE users ADD COLUMN IF NOT EXISTS locked_until TIMESTAMP;
ALTER TABLE users ADD COLUMN IF NOT EXISTS last_failed_login TIMESTAMP;
```

### 27.2 逻辑

**文件**: `OAuth2Server/AuthService.cc` (validateUser)

```cpp
// 登录前检查:
if (user.locked_until > now) {
    return error("account_locked", "Account locked until ...");
}

// 登录失败后:
user.failed_login_count++;
if (user.failed_login_count >= 5) {
    user.locked_until = now + exponentialBackoff(user.failed_login_count);
    // 5次: 1min, 10次: 5min, 15次: 30min, 20次: 永久锁定需管理员解锁
}

// 登录成功后:
user.failed_login_count = 0;
user.locked_until = null;
```

### 27.3 解锁方式

- 等待超时自动解锁
- 邮件验证解锁
- 管理员手动解锁 (`PUT /api/admin/users/:id/unlock`)

### 27.4 验收

```bash
# 1. 连续 5 次错误密码 → account_locked
# 2. 等待 1 分钟后 → 可再次尝试
# 3. 管理员 unlock → 立即可用
```

---

## Task 28: 分布式 Cleanup 锁 (P2-7)

### 28.1 实现

**文件**: `OAuth2Plugin/src/OAuth2CleanupService.cc`

```cpp
void OAuth2CleanupService::runCleanup() {
    // 尝试获取分布式锁
    redisClient_->execCommandAsync(
        [this](const RedisResult& r) {
            if (r.asString() == "OK") {
                // 获得锁，执行清理
                storage_->deleteExpiredData();
                // 设置锁 TTL (清理间隔的 80%)
            }
            // 未获得锁，跳过本轮
        },
        [](auto&&...) {},
        "SET oauth2:cleanup:lock %s NX EX %d",
        instanceId_.c_str(), lockTtlSeconds_
    );
}
```

**备选**: PostgreSQL Advisory Lock
```sql
SELECT pg_try_advisory_lock(12345);  -- 获取
-- 执行清理
SELECT pg_advisory_unlock(12345);    -- 释放
```

### 28.2 验收

```bash
# 启动 3 个实例，观察日志:
# 只有 1 个实例执行 cleanup，其他跳过
```

---

## Task 29: Token 表分区与归档 (P2-8)

### 29.1 PostgreSQL 分区

```sql
-- V018__token_partitioning.sql
-- 将 oauth2_access_tokens 改为按月分区
CREATE TABLE oauth2_access_tokens_new (
    LIKE oauth2_access_tokens INCLUDING ALL
) PARTITION BY RANGE (issued_at);

-- 创建分区 (按月)
CREATE TABLE oauth2_access_tokens_2026_05
    PARTITION OF oauth2_access_tokens_new
    FOR VALUES FROM (1746057600) TO (1748736000);

-- 自动创建未来分区的函数
CREATE OR REPLACE FUNCTION create_token_partition()
RETURNS void AS $$
-- ...
$$ LANGUAGE plpgsql;
```

### 29.2 归档策略

- 活跃分区: 当月 + 上月
- 冷存储: 3 个月前的分区 detach 并压缩存储
- 查询优化: `getAccessToken` 只扫描活跃分区

### 29.3 验收

```bash
# 1. 查询计划只扫描当前分区 (EXPLAIN ANALYZE)
# 2. 旧分区 detach 后不影响新 token 操作
# 3. 表大小增长可控
```

---

## 实施优先级建议

```
高优先 (业务需求驱动):
  Task 22 (Dynamic Registration) — 开放平台必备
  Task 25 (多租户) — SaaS 场景必备
  Task 27 (账号锁定) — 安全合规

中优先 (体验/安全增强):
  Task 23 (Device Code) — IoT/TV 场景
  Task 24 (Backchannel Logout) — SSO 场景
  Task 26 (WebAuthn) — 无密码趋势

低优先 (运维优化):
  Task 28 (分布式锁) — 多实例部署时
  Task 29 (分区归档) — 高 QPS 场景
```
