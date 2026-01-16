# OAuth2 Plugin 持久化与安全设计文档

本文档详细描述了 OAuth2 插件的持久化层设计、数据库 Schema、Redis 键值结构以及安全加固方案。

## 1. 设计目标

- **存储解耦**：通过 `IOAuth2Storage` 接口抽象，支持内存、PostgreSQL、Redis 等多种存储后端。
- **数据持久化**：确保 Client 信息、Token、Auth Code 等关键数据不丢失。
- **安全加固**：Client Secret 绝不明文存储，强制使用 SHA256 加盐哈希。
- **高稳定性**：核心存储操作采用同步 IO (`execSqlSync` / `execCommandSync`)，优先保证数据一致性和防范内存安全问题（如 Lambda 捕获临时变量声明周期问题）。

---

## 2. PostgreSQL 存储方案

适用于生产环境，提供严格的全部关系型数据一致性。

### 2.1 Database Schema

请在 PostgreSQL 中创建以下表结构：

#### 客户端表 (`oauth2_clients`)

存储接入的客户端应用信息。

```sql
CREATE TABLE oauth2_clients (
    client_id       VARCHAR(64) PRIMARY KEY,
    client_secret   VARCHAR(128) NOT NULL, -- 存储 SHA256(secret + salt) 的 Hex 字符串
    salt            VARCHAR(64) DEFAULT '', -- 随机盐值 (可选，建议使用)
    redirect_uris   TEXT NOT NULL,          -- JSON 数组格式: '["http://..."]'
    allowed_scopes  TEXT,                   -- JSON 数组格式: '["openid", "profile"]'
    created_at      TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at      TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
```

#### 授权码表 (`oauth2_authorization_codes`)

短期有效的授权凭证。

```sql
CREATE TABLE oauth2_authorization_codes (
    code            VARCHAR(64) PRIMARY KEY,
    client_id       VARCHAR(64) NOT NULL REFERENCES oauth2_clients(client_id),
    user_id         VARCHAR(128) NOT NULL,
    scope           TEXT,
    redirect_uri    TEXT NOT NULL,
    code_challenge  VARCHAR(128),         -- PKCE 支持
    code_challenge_method VARCHAR(10),     -- S256 / plain
    expires_at      TIMESTAMP NOT NULL,
    used            BOOLEAN DEFAULT FALSE, -- 防重放攻击
    created_at      TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
CREATE INDEX idx_auth_codes_expires ON oauth2_authorization_codes(expires_at);
```

#### 访问令牌表 (`oauth2_access_tokens`)

```sql
CREATE TABLE oauth2_access_tokens (
    token           VARCHAR(128) PRIMARY KEY,
    client_id       VARCHAR(64) NOT NULL REFERENCES oauth2_clients(client_id),
    user_id         VARCHAR(128) NOT NULL,
    scope           TEXT,
    expires_at      TIMESTAMP NOT NULL,
    revoked         BOOLEAN DEFAULT FALSE,
    created_at      TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
```

#### 刷新令牌表 (`oauth2_refresh_tokens`)

```sql
CREATE TABLE oauth2_refresh_tokens (
    token           VARCHAR(128) PRIMARY KEY,
    access_token    VARCHAR(128) NOT NULL REFERENCES oauth2_access_tokens(token),
    client_id       VARCHAR(64) NOT NULL,
    user_id         VARCHAR(128) NOT NULL,
    scope           TEXT,
    expires_at      TIMESTAMP NOT NULL,
    revoked         BOOLEAN DEFAULT FALSE,
    created_at      TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
```

---

## 3. Redis 存储方案

适用于高性能场景，利用 Redis TTL 自动管理 Token 过期。

### 3.1 Key Pattern 设计

所有 Key 均以 `oauth2:` 前缀开头。

| 实体 | Key 格式 | 类型 | TTL | 说明 |
|------|-------------|------|-----|------|
| **Client** | `oauth2:client:{client_id}` | Hash | 无 | 字段: `secret` (Hash), `salt`, `redirect_uris` (JSON), `allowed_scopes` (JSON) |
| **Auth Code** | `oauth2:code:{code}` | String | 10分钟 | Value: JSON 序列化对象 |
| **Access Token** | `oauth2:token:{token}` | String | 1小时 | Value: JSON 序列化对象 |
| **Refresh Token**| `oauth2:refresh:{token}` | String | 30天 | Value: JSON 序列化对象 |

### 3.2 示例数据

**Client (Hash Structure)**:

```bash
HSET oauth2:client:vue-client secret "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" salt "random_salt" redirect_uris "[\"http://localhost:5173/callback\"]"
```

**Auth Code (String Value)**:

```json
{
  "client_id": "vue-client",
  "user_id": "admin",
  "scope": "openid",
  "redirect_uri": "http://localhost:5173/callback",
  "expires_at": 1735689000
}
```

---

## 4. 安全加固 (Security Hardening)

为了防止数据库泄露导致 Client Secret 暴露，本系统实施了强制哈希策略。

### 4.1 算法与流程

1. **存储时**：
    - 生成随机 `salt`（可选，但在 Postgres Schema 中建议预留）。
    - 计算 `Hash = SHA256(raw_secret + salt)`。
    - 数据库存储 `Hash` (Hex String) 和 `salt`。

2. **验证时**：
    - 用户提交 `input_secret`。
    - 系统读取库中的 `stored_hash` 和 `salt`。
    - 计算 `CheckHash = SHA256(input_secret + salt)`。
    - 比对 `CheckHash` 与 `stored_hash` (忽略大小写)。

### 4.2 代码实现

位于 `RedisOAuth2Storage::validateClient` 和 `PostgresOAuth2Storage::validateClient` 中。

```cpp
// 核心逻辑示例
std::string input = clientSecret + client->salt;
std::string calculatedHash = drogon::utils::getSha256(input.data(), input.length());
return lower(calculatedHash) == lower(storedHash);
```

---

## 5. 架构决策：同步 vs 异步

### 现状

目前的持久化层（`RedisOAuth2Storage`, `PostgresOAuth2Storage`）均采用 **同步接口** (`execCommandSync`, `execSqlSync`)。

### 决策理由

在早期的异步改造（C++20 Coroutine）尝试中，发现 Drogon 的 Redis 异步接口在配合 Coroutines 使用时，如果处理不当（如引用捕获了临时的 `std::string` 参数），极易导致 **Use-After-Free** 崩溃。
为了保障 **OAuth2 认证服务器的绝对稳定性** 和 **数据一致性**，我们回退到了同步阻塞模型。

### 性能影响与未来计划

- 对于低并发、管理后台类应用（如当前的 OAuth2 Provider），同步 DB 操作的性能损耗完全可接受。
- 未来如果需要支撑高并发（如 10k+ QPS），可以重新引入 `drogon::Task<>` 协程，但必须配合 `shared_ptr` 进行严格的生命周期管理。
