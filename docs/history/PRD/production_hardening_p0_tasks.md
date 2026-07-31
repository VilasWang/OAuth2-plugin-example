# P0 Implementation Tasks — 安全底线

> 对应 `production_hardening_spec.md` 中 P0 章节  
> 每个 Task 包含：目标、涉及文件、具体改动点、验收方式

---

## Task 1: Token 强随机 + Hash 存储 (P0-1)

### 1.1 新增工具函数

**文件**: `OAuth2Plugin/src/common/utils/CryptoUtils.cc` (新建)  
**头文件**: `OAuth2Plugin/include/oauth2/CryptoUtils.h` (已存在，扩展)

```cpp
namespace oauth2::utils {
  // 生成 32 字节密码学安全随机数，返回 base64url 编码 (43 字符)
  std::string generateSecureToken(size_t bytes = 32);
  // SHA-256 hash，返回 hex 小写 (64 字符)
  std::string hashToken(const std::string& rawToken);
}
```

**实现**: 使用 OpenSSL `RAND_bytes` (Drogon 已链接 OpenSSL)。

### 1.2 替换 TokenService 中的 UUID 生成

**文件**: `OAuth2Plugin/src/services/TokenService.cc`

| 当前代码 (行号) | 改为 |
|---|---|
| L44: `auto code = drogon::utils::getUuid();` | `auto code = oauth2::utils::generateSecureToken();` |
| L144: `auto tokenStr = drogon::utils::getUuid();` | `auto tokenStr = oauth2::utils::generateSecureToken();` |
| L151: `auto refreshTokenStr = drogon::utils::getUuid();` | `auto refreshTokenStr = oauth2::utils::generateSecureToken();` |
| L207: `auto newTokenStr = drogon::utils::getUuid();` | `auto newTokenStr = oauth2::utils::generateSecureToken();` |
| L215: `auto newRefreshTokenStr = drogon::utils::getUuid();` | `auto newRefreshTokenStr = oauth2::utils::generateSecureToken();` |

### 1.3 Storage 层改为存 hash、查 hash

**文件**: `OAuth2Plugin/src/storage/PostgresOAuth2Storage.cc`

- `saveAuthCode`: 存入 DB 前 `authCode.code = hashToken(code)`，对外返回原始 code
- `consumeAuthCode`: 查询前 `auto hashedCode = hashToken(code)`
- `saveAccessToken`: `token.token = hashToken(rawToken)`
- `getAccessToken`: `auto hashedToken = hashToken(token)`
- `saveRefreshToken` / `getRefreshToken` / `revokeRefreshToken`: 同理

**文件**: `OAuth2Plugin/src/storage/RedisOAuth2Storage.cc` — 同样改法  
**文件**: `OAuth2Plugin/src/storage/MemoryOAuth2Storage.cc` — map key 改为 hash  
**文件**: `OAuth2Plugin/src/storage/CachedOAuth2Storage.cc` — cache key 改为 hash

### 1.4 迁移期双读（可选）

为避免升级瞬间所有已颁发 token 失效，`getAccessToken` 先查 hash key，miss 后查原始 key；
命中原始 key 时写入 hash key 并删除原始 key（渐进迁移）。

### 1.5 验收

```bash
# 1. 颁发 token 后检查 DB
psql -c "SELECT token FROM oauth2_access_tokens ORDER BY issued_at DESC LIMIT 1;"
# 应为 64 字符 hex (SHA-256)，而非 UUID 格式

# 2. 用返回的原始 token 调用 /oauth2/userinfo → 200
# 3. 用 DB 中的 hash 值调用 /oauth2/userinfo → 401
```

---

## Task 2: 密码哈希升级 Argon2id (P0-2)

### 2.1 引入依赖

**文件**: `conanfile.txt` 或 `CMakeLists.txt`

添加 `libsodium` (推荐) 或 `argon2` reference implementation。
libsodium 提供 `crypto_pwhash_str` (Argon2id) 开箱即用。

```cmake
# OAuth2Plugin/CMakeLists.txt
find_package(unofficial-sodium CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PUBLIC unofficial-sodium::sodium)
```

### 2.2 新增 PasswordHasher 工具

**文件**: `OAuth2Plugin/include/oauth2/PasswordHasher.h` (新建)  
**文件**: `OAuth2Plugin/src/common/utils/PasswordHasher.cc` (新建)

```cpp
namespace oauth2::utils {
class PasswordHasher {
public:
  // 生成 Argon2id hash (含 salt，输出格式: $argon2id$v=19$m=65536,t=3,p=1$...)
  static std::string hash(const std::string& password);
  // 验证密码（自动识别格式：argon2id 或 legacy sha256+salt）
  static bool verify(const std::string& password,
                     const std::string& storedHash,
                     const std::string& salt = "");
  // 判断是否需要升级（旧格式返回 true）
  static bool needsRehash(const std::string& storedHash);
};
}
```

### 2.3 修改 AuthService

**文件**: `OAuth2Server/AuthService.cc`

**validateUser** (L30-L50):
```cpp
// 替换:
// std::string inputHash = utils::getSha256(password + salt);
// 改为:
if (oauth2::utils::PasswordHasher::needsRehash(dbHash)) {
    // Legacy SHA-256 验证
    std::string inputHash = utils::getSha256(password + salt);
    valid = (toLower(inputHash) == toLower(dbHash));
    if (valid) {
        // 异步升级 hash
        auto newHash = oauth2::utils::PasswordHasher::hash(password);
        // UPDATE users SET password_hash=$1, salt='' WHERE id=$2
    }
} else {
    // Argon2id 验证 (salt 内嵌在 hash 中)
    valid = oauth2::utils::PasswordHasher::verify(password, dbHash);
}
```

**registerUser** (L75-L77):
```cpp
// 替换:
// std::string salt = utils::getUuid();
// std::string passwordHash = utils::getSha256(password + salt);
// 改为:
std::string salt = "";  // Argon2id 自带 salt
std::string passwordHash = oauth2::utils::PasswordHasher::hash(password);
```

### 2.4 Schema 变更

**文件**: `OAuth2Server/sql/005_password_hash_upgrade.sql` (新建)

```sql
ALTER TABLE users ALTER COLUMN password_hash TYPE VARCHAR(256);
-- Argon2id 输出约 97 字符，预留空间
```

### 2.5 验收

```bash
# 1. 注册新用户，检查 DB
psql -c "SELECT password_hash FROM users WHERE username='newuser';"
# 应以 $argon2id$ 开头

# 2. 旧用户 (admin) 登录成功后再查
psql -c "SELECT password_hash FROM users WHERE username='admin';"
# 应已升级为 $argon2id$ 格式
```

---

## Task 3: Refresh Token Reuse Detection (P0-3)

### 3.1 Schema 变更

**文件**: `OAuth2Server/sql/006_refresh_token_family.sql` (新建)

```sql
ALTER TABLE oauth2_refresh_tokens ADD COLUMN IF NOT EXISTS family_id VARCHAR(64);
CREATE INDEX IF NOT EXISTS idx_refresh_tokens_family ON oauth2_refresh_tokens(family_id);
```

### 3.2 数据结构变更

**文件**: `OAuth2Plugin/include/oauth2/IOAuth2Storage.h`

```cpp
struct OAuth2RefreshToken {
    // ... 现有字段 ...
    std::string familyId;  // 新增
};
```

### 3.3 TokenService 改造

**文件**: `OAuth2Plugin/src/services/TokenService.cc`

**exchangeCodeForToken** (L140-L160):
```cpp
// 首次颁发 RT 时生成 familyId
auto familyId = oauth2::utils::generateSecureToken(16);
refreshToken.familyId = familyId;
```

**refreshAccessToken** (L185-L230):
```cpp
// 1. 原子撤销旧 RT: UPDATE ... WHERE token=$1 AND revoked=false RETURNING *
//    如果返回空（已被撤销）→ 检测到 reuse
// 2. 若检测到 reuse:
//    - 撤销该 family 下所有 RT + 关联 AT
//    - 返回 invalid_grant
//    - 记录审计事件
// 3. 正常情况: 新 RT 继承 familyId
newRt.familyId = storedRt->familyId;
```

### 3.4 Storage 层新增接口

**文件**: `OAuth2Plugin/include/oauth2/IOAuth2Storage.h`

```cpp
// 原子撤销 refresh token (CAS)
virtual void atomicRevokeRefreshToken(
    const std::string& token,
    std::function<void(std::optional<OAuth2RefreshToken>)>&& cb) = 0;

// 按 family 级联撤销
virtual void revokeTokenFamily(
    const std::string& familyId,
    VoidCallback&& cb) = 0;
```

**实现文件**: 所有 4 个 Storage 实现都需要补充。

PostgreSQL 实现核心:
```sql
-- atomicRevokeRefreshToken
UPDATE oauth2_refresh_tokens
SET revoked = true, revoked_at = EXTRACT(EPOCH FROM NOW())
WHERE token = $1 AND revoked = false
RETURNING *;

-- revokeTokenFamily
UPDATE oauth2_refresh_tokens SET revoked = true WHERE family_id = $1;
UPDATE oauth2_access_tokens SET revoked = true
WHERE token IN (SELECT access_token FROM oauth2_refresh_tokens WHERE family_id = $1);
```

### 3.5 验收

```bash
# 1. 获取 refresh_token_A
# 2. 用 refresh_token_A 刷新 → 得到 refresh_token_B (A 被撤销)
# 3. 再次用 refresh_token_A 刷新 → 返回 invalid_grant
# 4. 用 refresh_token_B 刷新 → 也返回 invalid_grant (family 被撤销)
```

---

## Task 4: Auth Code 原子消费 + 事务化 (P0-4)

### 4.1 consumeAuthCode 改为原子操作

**文件**: `OAuth2Plugin/src/storage/PostgresOAuth2Storage.cc`

替换当前的 `findOne` + `update` 两步操作:

```cpp
void PostgresOAuth2Storage::consumeAuthCode(
    const std::string& code, const std::string& redirectUri, AuthCodeCallback&& cb)
{
    auto hashedCode = oauth2::utils::hashToken(code);
    auto sharedCb = std::make_shared<AuthCodeCallback>(std::move(cb));

    dbClientMaster_->execSqlAsync(
        "UPDATE oauth2_codes SET used = true "
        "WHERE code = $1 AND used = false "
        "RETURNING code, client_id, user_id, scope, redirect_uri, "
        "code_challenge, code_challenge_method, expires_at",
        [sharedCb, redirectUri](const drogon::orm::Result& r) {
            if (r.empty()) {
                (*sharedCb)(std::nullopt);
                return;
            }
            auto row = r[0];
            // 校验 redirect_uri
            if (!redirectUri.empty() && redirectUri != row["redirect_uri"].as<std::string>()) {
                (*sharedCb)(std::nullopt);
                return;
            }
            OAuth2AuthCode c;
            c.code = row["code"].as<std::string>();
            c.clientId = row["client_id"].as<std::string>();
            // ... 填充其余字段 ...
            (*sharedCb)(c);
        },
        [sharedCb](const DrogonDbException& e) {
            (*sharedCb)(std::nullopt);
        },
        hashedCode
    );
}
```

### 4.2 exchangeCodeForToken 事务化

**文件**: `OAuth2Plugin/src/services/TokenService.cc` (L130-L175)

```cpp
// 在 getUserRoles 回调内，改为:
auto dbClient = drogon::app().getDbClient();
auto transPtr = dbClient->newTransaction();

// saveAccessToken 和 saveRefreshToken 都使用 transPtr
// 任一失败 → transPtr->rollback()
// 全部成功 → transPtr 自动 commit (析构时)
```

**注意**: 需要在 `IOAuth2Storage` 接口中新增支持事务的重载，或在 TokenService 中直接操作 DB（破坏分层但更实际）。推荐方案：

```cpp
// IOAuth2Storage.h 新增
virtual void saveTokenPair(
    const OAuth2AccessToken& at,
    const OAuth2RefreshToken& rt,
    VoidCallback&& cb) = 0;  // 实现内部用事务
```

### 4.3 验收

```bash
# 压测: 100 并发用同一 code 请求 /oauth2/token
ab -n 100 -c 100 -p token_request.txt http://localhost:5555/oauth2/token
# 检查 DB: oauth2_access_tokens 中该 code 对应的 token 恰好 1 条
```

---

## Task 5: Subject UUID 化 (P0-5)

### 5.1 Schema 变更

**文件**: `OAuth2Server/sql/007_user_public_sub.sql` (新建)

```sql
ALTER TABLE users ADD COLUMN IF NOT EXISTS public_sub UUID DEFAULT gen_random_uuid();
CREATE UNIQUE INDEX IF NOT EXISTS idx_users_public_sub ON users(public_sub);
-- 回填现有用户
UPDATE users SET public_sub = gen_random_uuid() WHERE public_sub IS NULL;
ALTER TABLE users ALTER COLUMN public_sub SET NOT NULL;
```

### 5.2 修改 OAuth2Controller::login

**文件**: `OAuth2Server/controllers/OAuth2Controller.cc` (L230)

```cpp
// 替换:
// std::to_string(*userId)
// 改为: 查询 users.public_sub 并使用
// 或在 AuthService::validateUser 返回值中包含 public_sub
```

**文件**: `OAuth2Server/AuthService.h` / `.cc`

```cpp
// validateUser 返回结构体而非 optional<int>
struct AuthResult {
    int internalId;
    std::string publicSub;  // UUID string
};
static void validateUser(..., std::function<void(std::optional<AuthResult>)>&& cb);
```

### 5.3 修改 userinfo 响应

**文件**: `OAuth2Plugin/src/controllers/OAuth2StandardController.cc` (userInfo 方法)  
**文件**: `OAuth2Server/AuthService.cc` (getUserInfo)

```cpp
// json["sub"] = std::to_string(userId);
// 改为:
// json["sub"] = user.getValueOfPublicSub();
```

### 5.4 验收

```bash
curl -H "Authorization: Bearer $TOKEN" http://localhost:5555/oauth2/userinfo
# "sub" 字段应为 UUID 格式 (如 "550e8400-e29b-41d4-a716-446655440000")
# 不应出现数字 ID
```

---

## Task 6: 生产 Schema 安全化 (P0-6)

### 6.1 目录重组

```
OAuth2Server/
├── sql/
│   ├── migrations/          # 版本化迁移 (生产用)
│   │   ├── V001__initial_schema.sql
│   │   ├── V002__users_table.sql
│   │   ├── V003__rbac_schema.sql
│   │   ├── V004__oauth2_scopes.sql
│   │   ├── V005__password_hash_upgrade.sql
│   │   ├── V006__refresh_token_family.sql
│   │   └── V007__user_public_sub.sql
│   └── seed/                # 开发数据 (仅 dev/test)
│       ├── dev_admin_user.sql
│       └── dev_vue_client.sql
```

### 6.2 迁移表

```sql
CREATE TABLE IF NOT EXISTS schema_migrations (
    version INTEGER PRIMARY KEY,
    filename VARCHAR(255) NOT NULL,
    checksum VARCHAR(64) NOT NULL,
    applied_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
```

### 6.3 启动时自动迁移

**文件**: `OAuth2Server/main.cc` 或新建 `OAuth2Server/SchemaManager.cc`

```cpp
// 在 app().run() 之前:
// 1. 读取 sql/migrations/ 目录下所有 V*.sql
// 2. 查询 schema_migrations 已执行版本
// 3. 按版本号顺序执行未应用的迁移
// 4. 记录到 schema_migrations
```

### 6.4 清理现有 SQL ✅ 已完成

- 旧的 `001_oauth2_core.sql` ~ `004_oauth2_scopes.sql` 已删除
- 所有 schema 统一在 `sql/migrations/V001-V018` 管理
- 默认 admin 和 vue-client INSERT 已移到 `sql/seed/`
- 生产配置中不执行 seed

### 6.5 验收

```bash
# 从空库启动 → 自动建表，无报错
# 再次启动 → 不重复执行，schema_migrations 记录正确
# 生产模式启动 → 无 admin/admin 账号，无 vue-client
```

---

## Task 7: 配置安全与 HTTPS 强制 (P0-7)

### 7.1 环境变量覆盖

**文件**: `OAuth2Plugin/src/common/config/ConfigManager.cc`

已有 `applyEnvOverrides` 机制，需补充规则:

```cpp
static const std::vector<EnvOverride> prodOverrides = {
    {"OAUTH2_DB_HOST",     "db_clients.0.host"},
    {"OAUTH2_DB_PORT",     "db_clients.0.port"},
    {"OAUTH2_DB_NAME",     "db_clients.0.dbname"},
    {"OAUTH2_DB_USER",     "db_clients.0.user"},
    {"OAUTH2_DB_PASSWORD", "db_clients.0.passwd"},
    {"OAUTH2_REDIS_HOST",  "redis_clients.0.host"},
    {"OAUTH2_REDIS_PORT",  "redis_clients.0.port"},
    {"OAUTH2_REDIS_PASSWORD", "redis_clients.0.passwd"},
    {"OAUTH2_ISSUER",      "custom_config.metadata.issuer"},
    {"OAUTH2_LISTEN_PORT", "listeners.0.port"},
};
```

### 7.2 生产模式校验

**文件**: `OAuth2Plugin/src/common/config/ConfigManager.cc` (validate 方法)

```cpp
bool ConfigManager::validate(const Json::Value& config, std::string& error) {
    auto env = std::getenv("OAUTH2_ENV");
    bool isProd = (env && std::string(env) == "production");

    if (isProd) {
        // issuer 必须 https
        auto issuer = config["custom_config"]["metadata"]["issuer"].asString();
        if (issuer.find("https://") != 0) {
            error = "Production requires HTTPS issuer";
            return false;
        }
        // DB 密码不能是默认值
        auto dbPass = config["db_clients"][0]["passwd"].asString();
        if (dbPass == "123456" || dbPass.empty()) {
            error = "Production requires non-default DB password";
            return false;
        }
        // redirect_uri 不能含 localhost
        // ... 类似检查 ...
    }
    return true;
}
```

### 7.3 metadata issuer 一致性

**文件**: `OAuth2Plugin/src/controllers/OAuth2StandardController.cc` (metadata 方法)

```cpp
// 替换硬编码 "http://localhost:8080"
// 改为从配置读取，且与 token introspection 的 iss 字段一致
std::string baseUrl = app().getCustomConfig()["metadata"]["issuer"].asString();
if (baseUrl.empty()) {
    // 从 listener 推断（仅 dev 模式）
}
```

### 7.4 验收

```bash
# 设置 OAUTH2_ENV=production 但 issuer 为 http → 启动失败，日志输出原因
# 设置正确的 https issuer + 环境变量 → 启动成功
# GET /.well-known/oauth-authorization-server → issuer 字段为 https://...
```

---

## Task 8: AuthorizationFilter 默认拒绝 (P0-8)

### 8.1 修改 checkAccess 逻辑

**文件**: `OAuth2Plugin/src/filters/AuthorizationFilter.cc` (L140-L155)

```cpp
bool AuthorizationFilter::checkAccess(
    const std::vector<std::string>& userRoles, const std::string& path)
{
    // 新增: 检查 public_paths 白名单
    for (const auto& publicPath : publicPaths_) {
        if (std::regex_match(path, publicPath))
            return true;
    }

    bool matchedAnyRule = false;
    for (const auto& rule : rules_) {
        if (std::regex_match(path, rule.pathPattern)) {
            matchedAnyRule = true;
            for (const auto& allowed : rule.allowedRoles) {
                for (const auto& userRole : userRoles) {
                    if (userRole == allowed) return true;
                }
            }
        }
    }

    // 改动: 无论是否匹配规则，只要不在白名单且角色不匹配 → 拒绝
    return false;  // 原来是 !matchedAnyRule 时返回 true
}
```

### 8.2 新增 public_paths 配置

**文件**: `OAuth2Plugin/include/oauth2/filters/AuthorizationFilter.h`

```cpp
private:
    std::vector<std::regex> publicPaths_;  // 新增
```

**文件**: `OAuth2Plugin/src/filters/AuthorizationFilter.cc` (loadConfig)

```cpp
if (config.isMember("public_paths") && config["public_paths"].isArray()) {
    for (const auto& path : config["public_paths"]) {
        publicPaths_.push_back(std::regex(path.asString()));
    }
}
```

### 8.3 配置示例

**文件**: `OAuth2Server/config.json` (custom_config 段)

```json
"public_paths": [
    "^/health.*",
    "^/oauth2/authorize",
    "^/oauth2/token",
    "^/oauth2/login",
    "^/login",
    "^/api/register",
    "^/\\.well-known/.*",
    "^/docs/.*",
    "^/metrics"
]
```

### 8.4 验收

```bash
# 1. 不带 token 访问未配置规则的路径 → 401 (需要 token)
# 2. 带 token 但角色不匹配 → 403
# 3. 访问 /health → 200 (在 public_paths 中)
```

---

## Task 9: Health Check 真实探活 (P0-9)

### 9.1 拆分端点

**文件**: `OAuth2Server/controllers/OAuth2Controller.h`

```cpp
ADD_METHOD_TO(OAuth2Controller::healthLive, "/health/live", Get);
ADD_METHOD_TO(OAuth2Controller::healthReady, "/health/ready", Get);
```

### 9.2 实现

**文件**: `OAuth2Server/controllers/OAuth2Controller.cc`

```cpp
void OAuth2Controller::healthLive(const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback) {
    Json::Value json;
    json["status"] = "ok";
    callback(HttpResponse::newHttpJsonResponse(json));
}

void OAuth2Controller::healthReady(const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback) {
    auto db = drogon::app().getDbClient();
    db->execSqlAsync("SELECT 1",
        [callback](const Result& r) {
            // DB OK, check Redis
            try {
                auto redis = drogon::app().getRedisClient("default");
                redis->execCommandAsync(
                    [callback](const drogon::nosql::RedisResult& r) {
                        Json::Value json;
                        json["status"] = "ok";
                        json["database"] = "connected";
                        json["redis"] = "connected";
                        callback(HttpResponse::newHttpJsonResponse(json));
                    },
                    [callback](const std::exception& e) {
                        Json::Value json;
                        json["status"] = "degraded";
                        json["database"] = "connected";
                        json["redis"] = "disconnected";
                        auto resp = HttpResponse::newHttpJsonResponse(json);
                        resp->setStatusCode(k503ServiceUnavailable);
                        callback(resp);
                    },
                    "PING"
                );
            } catch (...) {
                Json::Value json;
                json["status"] = "degraded";
                json["database"] = "connected";
                json["redis"] = "unavailable";
                auto resp = HttpResponse::newHttpJsonResponse(json);
                resp->setStatusCode(k503ServiceUnavailable);
                callback(resp);
            }
        },
        [callback](const DrogonDbException& e) {
            Json::Value json;
            json["status"] = "unhealthy";
            json["database"] = "disconnected";
            auto resp = HttpResponse::newHttpJsonResponse(json);
            resp->setStatusCode(k503ServiceUnavailable);
            callback(resp);
        }
    );
}
```

### 9.3 验收

```bash
# 正常: GET /health/live → 200, GET /health/ready → 200
# 停 DB: GET /health/ready → 503, /health/live → 200
# 停 Redis: GET /health/ready → 503 (或 degraded 模式可配置)
```

---

## Task 10: handleFirstTimeLogin 修复 (P0-10)

### 10.1 修改 IdentityService

**文件**: `OAuth2Plugin/src/services/IdentityService.cc` (L55-L70)

```cpp
void IdentityService::handleFirstTimeLogin(
    const std::string& subject, const std::string& provider,
    std::function<void(int32_t)>&& callback)
{
    if (!storage_) { callback(0); return; }

    auto [prov, sub] = utils::SubjectGenerator::parse(subject);

    // 替换 static int32_t nextUserId = 1000;
    // 改为: INSERT INTO users (...) RETURNING id
    storage_->createUserForExternalLogin(
        sub, prov,
        [this, sub, prov, callback = std::move(callback)](std::optional<int32_t> newUserId) {
            if (!newUserId) { callback(0); return; }
            storage_->createSubjectMapping(
                sub, *newUserId, prov,
                [newUserId = *newUserId, callback](bool success) {
                    callback(success ? newUserId : 0);
                }
            );
        }
    );
}
```

### 10.2 Storage 接口新增

**文件**: `OAuth2Plugin/include/oauth2/IOAuth2Storage.h`

```cpp
// 为外部登录创建用户记录
virtual void createUserForExternalLogin(
    const std::string& externalId,
    const std::string& provider,
    std::function<void(std::optional<int32_t>)>&& cb) = 0;
```

**PostgreSQL 实现**:
```sql
INSERT INTO users (username, password_hash, salt, email, public_sub)
VALUES ($1, 'EXTERNAL_AUTH', '', '', gen_random_uuid())
RETURNING id;
-- username 可用 provider:externalId 或生成随机用户名
```

### 10.3 验收

```bash
# 1. 启动两个进程实例
# 2. 同时用新的 Google 账号登录
# 3. 两个实例不会产生 ID 冲突
# 4. users 表中有对应记录
```

---

## 实施顺序建议

```
Week 1:
  Day 1-2: Task 6 (Schema 重组) — 基础设施，其他 task 依赖
  Day 3:   Task 1 (Token 强随机 + hash) — 核心安全
  Day 4:   Task 2 (Argon2id) — 独立模块
  Day 5:   Task 5 (Subject UUID) — 需要 schema 变更

Week 2:
  Day 1-2: Task 3 (RT Reuse Detection) — 最复杂
  Day 3:   Task 4 (原子消费 + 事务) — 与 Task 3 关联
  Day 4:   Task 7 (配置安全) + Task 8 (默认拒绝)
  Day 5:   Task 9 (Health) + Task 10 (handleFirstTimeLogin)

Week 3:
  Day 1-2: 集成测试 + 回归测试
  Day 3:   性能测试 (并发 token 兑换、RT 刷新)
  Day 4-5: 文档更新 + Code Review
```

---

## 依赖关系图

```
Task 6 (Schema) ─────┬──→ Task 1 (Token Hash)
                     ├──→ Task 2 (Argon2id)
                     ├──→ Task 3 (RT Family) ──→ Task 4 (事务)
                     └──→ Task 5 (Subject UUID)

Task 7 (配置) ←── 独立
Task 8 (默认拒绝) ←── 独立
Task 9 (Health) ←── 独立
Task 10 (FirstTimeLogin) ←── Task 5 (需要 public_sub)
```

---

## 新增/修改文件汇总

| 操作 | 文件路径 |
|------|----------|
| 新建 | `OAuth2Plugin/src/common/utils/CryptoUtils.cc` |
| 新建 | `OAuth2Plugin/src/common/utils/PasswordHasher.cc` |
| 新建 | `OAuth2Plugin/include/oauth2/PasswordHasher.h` |
| 新建 | `OAuth2Server/sql/migrations/V005__password_hash_upgrade.sql` |
| 新建 | `OAuth2Server/sql/migrations/V006__refresh_token_family.sql` |
| 新建 | `OAuth2Server/sql/migrations/V007__user_public_sub.sql` |
| 新建 | `OAuth2Server/sql/seed/dev_admin_user.sql` |
| 新建 | `OAuth2Server/sql/seed/dev_vue_client.sql` |
| 新建 | `OAuth2Server/SchemaManager.h` / `.cc` |
| 修改 | `OAuth2Plugin/include/oauth2/CryptoUtils.h` |
| 修改 | `OAuth2Plugin/include/oauth2/IOAuth2Storage.h` |
| 修改 | `OAuth2Plugin/src/services/TokenService.cc` |
| 修改 | `OAuth2Plugin/src/services/IdentityService.cc` |
| 修改 | `OAuth2Plugin/src/storage/PostgresOAuth2Storage.cc` |
| 修改 | `OAuth2Plugin/src/storage/RedisOAuth2Storage.cc` |
| 修改 | `OAuth2Plugin/src/storage/MemoryOAuth2Storage.cc` |
| 修改 | `OAuth2Plugin/src/storage/CachedOAuth2Storage.cc` |
| 修改 | `OAuth2Plugin/src/filters/AuthorizationFilter.cc` |
| 修改 | `OAuth2Plugin/include/oauth2/filters/AuthorizationFilter.h` |
| 修改 | `OAuth2Plugin/src/controllers/OAuth2StandardController.cc` |
| 修改 | `OAuth2Plugin/src/common/config/ConfigManager.cc` |
| 修改 | `OAuth2Plugin/CMakeLists.txt` (添加 libsodium) |
| 修改 | `OAuth2Server/AuthService.h` / `.cc` |
| 修改 | `OAuth2Server/controllers/OAuth2Controller.h` / `.cc` |
| 修改 | `OAuth2Server/main.cc` |
| 修改 | `OAuth2Server/config.json` / `config.prod.json` |
| 重组 | `OAuth2Server/sql/` → `migrations/` + `seed/` |
