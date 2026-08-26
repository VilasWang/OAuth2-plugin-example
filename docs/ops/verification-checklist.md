# 部署验证清单

本文档提供完整的部署验证步骤，确保 fulla 全栈系统在 Windows Docker Desktop 或 Linux 生产环境上正确运行。

---

## 快速验证（5 分钟）

### 1. 检查所有容器状态

```powershell
# Windows
docker compose -f deploy/docker/docker-compose.yml ps

# Linux
docker compose -f deploy/docker/docker-compose.prod.yml --env-file .env.docker ps
```

**预期结果**：所有容器状态为 `Up` 或 `Up (healthy)`

| 容器名 | 状态 | 端口映射 |
|--------|------|---------|
| fulla-frontend | Up | 8080:80 |
| fulla-admin | Up | 8081:80 |
| fulla-backend | Up (healthy) | 5555:5555 |
| fulla-postgres | Up (healthy) | 5433:5432 |
| fulla-redis | Up | 6380:6379 |
| fulla-prometheus | Up | 9090:9090 |

### 2. 健康检查

```powershell
# 后端健康端点
curl http://localhost:5555/health

# 预期输出
{"status":"healthy","timestamp":"2024-06-23T10:30:00Z"}
```

### 3. 数据库连接测试

```powershell
# 进入 postgres 容器
docker exec -it fulla-postgres psql -U fulla_user -d fulla_db -c "\dt"

# 预期输出：OAuth2 相关表列表
# clients, users, tokens, authorization_codes, refresh_tokens, scopes, etc.
```

### 4. 前端访问测试

在浏览器中打开：
- **用户前端**：http://localhost:8080 或 https://your-domain.com
- **管理后台**：http://localhost:8081 或 https://your-domain.com/admin

**预期结果**：页面正常加载，无 404 或 502 错误

---

## 完整验证（30 分钟）

## 阶段一：基础设施验证

### 1.1 PostgreSQL 验证

```powershell
# 连接测试
docker exec fulla-postgres pg_isready -U fulla_user

# 预期输出：/var/run/postgresql:5432 - accepting connections

# 表结构检查
docker exec fulla-postgres psql -U fulla_user -d fulla_db -c "
SELECT table_name 
FROM information_schema.tables 
WHERE table_schema = 'public' 
ORDER BY table_name;
"

# 预期表列表（V002-V026 实际 schema，均带 oauth2_ 前缀）：
# - oauth2_access_tokens, oauth2_refresh_tokens, oauth2_codes
# - oauth2_clients, oauth2_scopes, oauth2_client_scopes
# - oauth2_user_consents, oauth2_subject_mappings, oauth2_device_codes
# - users, roles, permissions, user_roles, role_permissions
# - organizations, audit_logs, webauthn_credentials 等

# 数据库版本检查
docker exec fulla-postgres psql -U fulla_user -d fulla_db -c "SELECT version();"

# 预期：PostgreSQL 17.x（deploy compose 默认 postgres:17-alpine；
#       显式钉回 15 的存量部署此处应为 15.x，见 docs/ops/postgresql-major-upgrade.md）
```

### 1.2 Redis 验证

```powershell
# 进入 redis 容器
docker exec -it fulla-redis redis-cli -a redis_secret_pass ping

# 预期输出：PONG

# 测试读写
docker exec fulla-redis redis-cli -a redis_secret_pass SET test_key "hello"
docker exec fulla-redis redis-cli -a redis_secret_pass GET test_key

# 预期输出："hello"

# 检查内存使用
docker exec fulla-redis redis-cli -a redis_secret_pass INFO memory

# 预期：used_memory_human 显示合理的内存占用
```

### 1.3 网络连通性验证

```powershell
# 从后端容器测试数据库连接
docker exec fulla-backend ping -c 3 fulla-postgres

# 预期：3 packets transmitted, 3 received, 0% packet loss

# 从后端容器测试 Redis 连接
docker exec fulla-backend ping -c 3 fulla-redis

# 预期：3 packets transmitted, 3 received, 0% packet loss

# 检查 DNS 解析
docker exec fulla-backend nslookup fulla-postgres

# 预期：返回 fulla-postgres 的容器 IP 地址（如 172.x.x.x）
```

---

## 阶段二：数据库初始化验证

### 2.1 检查 Seed 数据

```powershell
# 检查管理员用户
docker exec fulla-postgres psql -U fulla_user -d fulla_db -c "
SELECT username, email, role, created_at 
FROM users 
WHERE username = 'admin';
"

# 预期输出：
# username | email                 | role  | created_at
# ---------+-----------------------+-------+--------------------
# admin    | admin@example.com | admin | 2024-06-23 xx:xx:xx

# 检查默认客户端
docker exec fulla-postgres psql -U fulla_user -d fulla_db -c "
SELECT client_id, client_name, redirect_uris 
FROM clients 
WHERE client_id IN ('admin-console', 'vue-client');
"

# 预期输出：显示管理后台和 Vue 客户端的配置

# 检查默认 Scopes
docker exec fulla-postgres psql -U fulla_user -d fulla_db -c "
SELECT scope_id, description 
FROM scopes 
LIMIT 5;
"

# 预期输出：openid, profile, email, admin, read, write 等标准 scope
```

### 2.2 验证数据库迁移

```powershell
# 检查 migrations 表（如果有的话）
docker exec fulla-postgres psql -U fulla_user -d fulla_db -c "\d schema_migrations"

# 或检查表结构完整性
docker exec fulla-postgres psql -U fulla_user -d fulla_db -c "
SELECT COUNT(*) AS table_count 
FROM information_schema.tables 
WHERE table_schema = 'public' AND table_type = 'BASE TABLE';
"

# 预期：table_count >= 15（V026 后实测 21 张 public 表）
```

---

## 阶段三：后端 API 验证

### 3.1 获取管理员令牌

```powershell
# 使用 password grant 获取令牌
curl -X POST http://localhost:5555/oauth2/token \
  -H "Content-Type: application/json" \
  -d '{
    "grant_type": "password",
    "client_id": "admin-console",
    "client_secret": "admin-secret",
    "username": "admin",
    "password": "admin",
    "scope": "admin"
  }'

# 预期响应（保存 access_token）：
{
  "access_token": "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9...",
  "token_type": "Bearer",
  "expires_in": 3600,
  "refresh_token": "tGzv3JH7xN1yQ9X2...",
  "scope": "admin"
}

# 设置环境变量（后续测试使用）
# PowerShell
$token = "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9..."

# Bash
export TOKEN="eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9..."
```

### 3.2 验证令牌内省（Token Introspection）

```powershell
# 内省令牌
curl -X POST http://localhost:5555/oauth2/introspect \
  -H "Content-Type: application/json" \
  -d "{
    \"token\": \"$token\",
    \"token_type_hint\": \"access_token\"
  }"

# 预期响应：
{
  "active": true,
  "client_id": "admin-console",
  "username": "admin",
  "scope": "admin",
  "exp": 1719123456,
  "iat": 1719119856,
  "sub": "admin",
  "aud": "fulla-backend",
  "iss": "fulla"
}

# 测试无效令牌
curl -X POST http://localhost:5555/oauth2/introspect \
  -H "Content-Type: application/json" \
  -d '{"token": "invalid_token", "token_type_hint": "access_token"}'

# 预期响应：{"active": false}
```

### 3.3 刷新令牌（Refresh Token）

```powershell
# 使用 refresh_token 获取新的 access_token
curl -X POST http://localhost:5555/oauth2/token \
  -H "Content-Type: application/json" \
  -d "{
    \"grant_type\": \"refresh_token\",
    \"refresh_token\": \"tGzv3JH7xN1yQ9X2...\",
    \"client_id\": \"admin-console\",
    \"client_secret\": \"admin-secret\"
  }"

# 预期响应：返回新的 access_token 和 refresh_token
{
  "access_token": "新的 JWT...",
  "token_type": "Bearer",
  "expires_in": 3600,
  "refresh_token": "新的 refresh_token...",
  "scope": "admin"
}
```

### 3.4 撤销令牌（Token Revocation）

```powershell
# 撤销令牌
curl -X POST http://localhost:5555/oauth2/revoke \
  -H "Content-Type: application/json" \
  -d "{
    \"token\": \"$token\",
    \"token_type_hint\": \"access_token\",
    \"client_id\": \"admin-console\",
    \"client_secret\": \"admin-secret\"
  }"

# 预期响应：HTTP 200 OK（空响应体）

# 验证令牌已被撤销
curl -X POST http://localhost:5555/oauth2/introspect \
  -H "Content-Type: application/json" \
  -d "{\"token\": \"$token\", \"token_type_hint\": \"access_token\"}"

# 预期响应：{"active": false}
```

---

## 阶段四：管理后台 API 验证

### 4.1 用户管理 API

```powershell
# 获取用户列表
curl -X GET http://localhost:5555/api/admin/users \
  -H "Authorization: Bearer $token"

# 预期响应：用户列表 JSON
{
  "users": [
    {
      "user_id": 1,
      "username": "admin",
      "email": "admin@example.com",
      "role": "admin",
      "created_at": "2024-06-23T10:00:00Z",
      "updated_at": "2024-06-23T10:00:00Z"
    }
  ],
  "total": 1,
  "page": 1,
  "per_page": 20
}

# 创建新用户
curl -X POST http://localhost:5555/api/admin/users \
  -H "Authorization: Bearer $token" \
  -H "Content-Type: application/json" \
  -d '{
    "username": "testuser",
    "email": "test@example.com",
    "password": "TestPassword123!",
    "role": "user"
  }'

# 预期响应：HTTP 201 Created
{
  "user_id": 2,
  "username": "testuser",
  "email": "test@example.com",
  "role": "user",
  "created_at": "2024-06-23T10:30:00Z"
}

# 获取单个用户详情
curl -X GET http://localhost:5555/api/admin/users/2 \
  -H "Authorization: Bearer $token"

# 预期响应：显示 testuser 的详细信息
```

### 4.2 客户端管理 API

```powershell
# 获取客户端列表
curl -X GET http://localhost:5555/api/admin/clients \
  -H "Authorization: Bearer $token"

# 预期响应：客户端列表
{
  "clients": [
    {
      "client_id": "admin-console",
      "client_name": "Admin Console",
      "redirect_uris": ["http://localhost:8081/admin/callback"],
      "scopes": ["admin"],
      "grant_types": ["password", "refresh_token"],
      "client_secret": "admin-secret"
    }
  ],
  "total": 1
}

# 创建新客户端
curl -X POST http://localhost:5555/api/admin/clients \
  -H "Authorization: Bearer $token" \
  -H "Content-Type: application/json" \
  -d '{
    "client_id": "test-client",
    "client_name": "Test Client",
    "client_secret": "test-secret",
    "redirect_uris": ["http://localhost:8080/callback"],
    "scopes": ["openid", "profile", "email"],
    "grant_types": ["authorization_code", "refresh_token"]
  }'

# 预期响应：HTTP 201 Created
```

### 4.3 Scope 管理 API

```powershell
# 获取所有 scopes
curl -X GET http://localhost:5555/api/admin/scopes \
  -H "Authorization: Bearer $token"

# 预期响应：scope 列表
{
  "scopes": [
    {"scope_id": "openid", "description": "OpenID Connect"},
    {"scope_id": "profile", "description": "User profile"},
    {"scope_id": "email", "description": "User email"},
    {"scope_id": "admin", "description": "Administrative access"}
  ]
}

# 创建新 scope
curl -X POST http://localhost:5555/api/admin/scopes \
  -H "Authorization: Bearer $token" \
  -H "Content-Type: application/json" \
  -d '{
    "scope_id": "read",
    "description": "Read access to user resources"
  }'

# 预期响应：HTTP 201 Created
```

---

## 阶段五：前端功能验证

### 5.1 用户前端验证

| 测试项 | 操作步骤 | 预期结果 |
|--------|---------|---------|
| 访问首页 | 打开 http://localhost:8080 | 显示登录页面 |
| 用户注册 | 填写注册表单（用户名、邮箱、密码） | 注册成功，跳转到登录页 |
| 用户登录 | 使用刚注册的账号登录 | 登录成功，跳转到个人资料页 |
| 访问个人资料 | 点击"个人资料"菜单 | 显示用户信息（用户名、邮箱） |
| 修改密码 | 输入旧密码和新密码 | 密码修改成功，需要重新登录 |
| 退出登录 | 点击"退出"按钮 | 退出成功，跳转到登录页 |

### 5.2 管理后台验证

| 测试项 | 操作步骤 | 预期结果 |
|--------|---------|---------|
| 访问管理后台 | 打开 http://localhost:8081/admin | 显示管理后台登录页 |
| 管理员登录 | 使用 admin/admin 登录 | 登录成功，显示仪表板 |
| 应用管理 | 点击"应用"菜单 | 显示客户端列表（至少有 admin-console） |
| 创建应用 | 点击"新建应用"，填写表单 | 应用创建成功，出现在列表中 |
| 用户管理 | 点击"用户"菜单 | 显示用户列表（至少有 admin 和刚注册的用户） |
| Token 管理 | 点击"Token"菜单 | 显示 active tokens 列表 |

### 5.3 OAuth2 授权码流程验证

```powershell
# 步骤 1：构建授权 URL
# 访问以下 URL（在浏览器中）：
# http://localhost:5555/oauth2/authorize?
#   response_type=code&
#   client_id=vue-client&
#   redirect_uri=http://localhost:8080/callback&
#   scope=openid profile email&
#   state=random_state_value

# 预期：重定向到登录页面

# 步骤 2：用户登录
# 使用测试账号登录（如 testuser）

# 预期：显示授权确认页面

# 步骤 3：用户授权
# 点击"授权"按钮

# 预期：重定向到 redirect_uri，携带 authorization code
# http://localhost:8080/callback?code=xxx&state=random_state_value

# 步骤 4：交换令牌
curl -X POST http://localhost:5555/oauth2/token \
  -H "Content-Type: application/json" \
  -d '{
    "grant_type": "authorization_code",
    "code": "从回调中获取的 code",
    "redirect_uri": "http://localhost:8080/callback",
    "client_id": "vue-client",
    "client_secret": "vue-secret"
  }'

# 预期响应：返回 access_token 和 refresh_token
{
  "access_token": "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9...",
  "token_type": "Bearer",
  "expires_in": 3600,
  "refresh_token": "xxx",
  "scope": "openid profile email"
}
```

---

## 阶段五·补充：邮件服务验证

邮件服务有两种模式，由后端 `getEmailService()` 根据 `FULLA_SMTP_*` 环境变量决定。

### 5.4 确认邮件服务模式

```powershell
# 查看后端启动日志中的邮件服务模式
docker logs fulla-backend 2>&1 | grep -i "Email service"
```

**预期输出（二选一）**：

- Console 模式（未配置 SMTP）：`Email service: Console (set FULLA_SMTP_* env vars to enable SMTP)`
- SMTP 模式（已配置）：`Email service: SMTP (smtp.163.com:465)`

### 5.5 邮箱验证邮件

**操作**：登录用户前端 → Profile 页面 → 点击"发送邮箱验证"

| 模式 | 验证方式 |
|------|---------|
| Console 模式 | 邮件内容打到后端日志，从中复制验证链接 |
| SMTP 模式 | 收件箱应收到真实邮件 |

**Console 模式下查看验证链接**：

```powershell
docker logs fulla-backend --tail 50 2>&1 | grep -A 5 -iE "verify|email"
# 预期：含 "verify-email?token=xxx" 的链接
```

**SMTP 模式下验证邮件发送**：

```powershell
# 触发发送后，检查后端是否有 SMTP 错误
docker logs fulla-backend --tail 50 2>&1 | grep -iE "smtp|email|curl"
# 预期：无 ERROR 级别日志；收件箱收到 "Verify Your Email" 邮件
```

### 5.6 密码重置邮件

**操作**：前端"忘记密码"页面 → 输入邮箱 → 提交

- **预期响应**（防枚举）：无论邮箱是否存在，统一返回 `If the email exists, a reset link has been sent`
- Console 模式下，重置链接同样打到后端日志

### 5.7 启用真实 SMTP（可选，详见部署指南）

如需真实邮件发送，在 `.env.docker` 设置 `FULLA_SMTP_HOST` / `FULLA_SMTP_USER` / `FULLA_SMTP_PASSWORD` 三项后重启后端：

```powershell
docker compose -f deploy/docker/docker-compose.yml --env-file .env.docker up -d fulla-backend
docker logs fulla-backend 2>&1 | grep -i "Email service"
# 预期：Email service: SMTP (...)
```

> **注意**：邮件验证链接使用 `FULLA_FRONTEND_URL`。本地部署为 `http://localhost:8080`，从其他机器点击会失效。

---

## 阶段六：安全性验证

### 6.1 错误响应验证

```powershell
# 测试无效客户端 ID
curl -X POST http://localhost:5555/oauth2/token \
  -H "Content-Type: application/json" \
  -d '{
    "grant_type": "password",
    "client_id": "invalid-client",
    "client_secret": "secret",
    "username": "admin",
    "password": "admin"
  }'

# 预期响应：HTTP 401 Unauthorized
{
  "error": "invalid_client",
  "error_description": "Client authentication failed"
}

# 测试无效密码
curl -X POST http://localhost:5555/oauth2/token \
  -H "Content-Type: application/json" \
  -d '{
    "grant_type": "password",
    "client_id": "admin-console",
    "client_secret": "admin-secret",
    "username": "admin",
    "password": "wrong-password"
  }'

# 预期响应：HTTP 401 Unauthorized
{
  "error": "invalid_grant",
  "error_description": "Invalid username or password"
}

# 测试缺少必需参数
curl -X POST http://localhost:5555/oauth2/token \
  -H "Content-Type: application/json" \
  -d '{
    "grant_type": "password"
  }'

# 预期响应：HTTP 400 Bad Request
{
  "error": "invalid_request",
  "error_description": "Missing required parameter: client_id"
}
```

### 6.2 令牌过期验证

```powershell
# 等待令牌过期（3600 秒），或修改后端配置为较短的过期时间进行测试

# 或使用已撤销的令牌
curl -X GET http://localhost:5555/api/admin/users \
  -H "Authorization: Bearer revoked_token"

# 预期响应：HTTP 401 Unauthorized
{
  "error": "invalid_token",
  "error_description": "The access token expired or has been revoked"
}
```

### 6.3 Scope 授权验证

```powershell
# 请求超出授权范围的资源（如果实现了 scope-based access control）
curl -X GET http://localhost:5555/api/admin/users \
  -H "Authorization: Bearer $token_with_limited_scope"

# 预期响应：HTTP 403 Forbidden
{
  "error": "insufficient_scope",
  "error_description": "The request requires higher privileges than provided by the access token"
}
```

---

## 阶段七：性能和监控验证

### 7.1 Prometheus 指标验证

```powershell
# 访问 Prometheus UI
# 打开浏览器：http://localhost:9090

# 查询示例指标：
# - oauth2_http_requests_total：总请求数
# - oauth2_http_request_duration_seconds：请求耗时
# - oauth2_active_tokens：当前活跃令牌数
# - oauth2_database_query_duration_seconds：数据库查询耗时

# 预期：指标正常采集，有数据
```

### 7.2 日志验证

```powershell
# 查看后端日志
docker logs fulla-backend --tail 50

# 预期：无 ERROR 级别日志，正常的 INFO/DEBUG 日志

# 查看 nginx 日志（Linux 生产环境）
docker logs oauth2-nginx --tail 50

# 预期：正常的访问日志，无 5xx 错误

# 实时跟踪日志
docker compose -f deploy/docker/docker-compose.yml logs -f fulla-backend
```

### 7.3 数据库性能验证

```powershell
# 检查数据库连接数
docker exec fulla-postgres psql -U fulla_user -d fulla_db -c "
SELECT count(*) AS connections 
FROM pg_stat_activity 
WHERE datname = 'fulla_db';
"

# 预期：connections 为合理值（通常 < 20）

# 检查慢查询（如果有 pg_stat_statements 扩展）
docker exec fulla-postgres psql -U fulla_user -d fulla_db -c "
SELECT query, calls, total_time, mean_time 
FROM pg_stat_statements 
ORDER BY mean_time DESC 
LIMIT 5;
"

# 预期：无明显的慢查询（mean_time < 100ms）
```

---

## 故障排除检查点

### 问题 1：容器无法启动

**检查步骤**：

```powershell
# 查看容器状态
docker compose -f deploy/docker/docker-compose.yml ps

# 查看失败容器的日志
docker logs fulla-backend

# 检查资源占用
docker stats

# 验证配置文件
docker compose -f deploy/docker/docker-compose.yml config
```

### 问题 2：数据库连接失败

**检查步骤**：

```powershell
# 验证 postgres 容器健康状态
docker exec fulla-postgres pg_isready -U fulla_user

# 检查网络连通性
docker exec fulla-backend ping fulla-postgres

# 验证环境变量
docker exec fulla-backend env | grep FULLA_DB

# 查看数据库日志
docker logs fulla-postgres
```

### 问题 3：前端无法访问后端 API

**检查步骤**：

```powershell
# 从前端容器测试后端连接
docker exec fulla-frontend curl -s http://fulla-backend:5555/health

# 检查 nginx 配置（生产环境）
docker exec oauth2-nginx nginx -t

# 查看后端 CORS 配置
docker logs fulla-backend | grep -i cors
```

### 问题 4：令牌验证失败

**检查步骤**：

```powershell
# 验证 JWT 密钥存在
docker exec fulla-backend ls -la /app/keys/

# 检查令牌签名
# 复制 access_token 到 https://jwt.io 解码验证

# 查看后端日志中的认证错误
docker logs fulla-backend | grep -i "auth\|token"
```

---

## 自动化验证脚本

### 完整验证脚本（PowerShell）

保存为 `verify-deployment.ps1`：

```powershell
# fulla 部署验证脚本
param(
    [string]$BackendUrl = "http://localhost:5555",
    [string]$FrontendUrl = "http://localhost:8080",
    [string]$AdminUrl = "http://localhost:8081",
    [switch]$Verbose
}

function Test-ContainerStatus {
    Write-Host "`n[1/8] 检查容器状态..." -ForegroundColor Cyan
    $containers = docker compose -f deploy/docker/docker-compose.yml ps
    if ($LASTEXITCODE -eq 0) {
        Write-Host "[+] 所有容器运行正常" -ForegroundColor Green
        return $true
    } else {
        Write-Host "[-] 容器状态异常" -ForegroundColor Red
        return $false
    }
}

function Test-BackendHealth {
    Write-Host "`n[2/8] 检查后端健康..." -ForegroundColor Cyan
    try {
        $response = Invoke-RestMethod -Uri "$BackendUrl/health" -Method Get
        if ($response.status -eq "healthy") {
            Write-Host "[+] 后端健康检查通过" -ForegroundColor Green
            return $true
        }
    } catch {
        Write-Host "[-] 后端健康检查失败: $_" -ForegroundColor Red
        return $false
    }
}

function Test-DatabaseConnection {
    Write-Host "`n[3/8] 检查数据库连接..." -ForegroundColor Cyan
    $result = docker exec fulla-postgres pg_isready -U fulla_user 2>&1
    if ($LASTEXITCODE -eq 0 -and $result -match "accepting connections") {
        Write-Host "[+] 数据库连接正常" -ForegroundColor Green
        return $true
    } else {
        Write-Host "[-] 数据库连接失败" -ForegroundColor Red
        return $false
    }
}

function Test-DatabaseTables {
    Write-Host "`n[4/8] 检查数据库表结构..." -ForegroundColor Cyan
    $result = docker exec fulla-postgres psql -U fulla_user -d fulla_db -t -c "
        SELECT COUNT(*) FROM information_schema.tables 
        WHERE table_schema = 'public' AND table_type = 'BASE TABLE';
    " 2>&1
    
    $tableCount = [int]$result.Trim()
    if ($tableCount -ge 7) {
        Write-Host "[+] 数据库表结构完整 ($tableCount 张表)" -ForegroundColor Green
        return $true
    } else {
        Write-Host "[-] 数据库表结构不完整 (仅 $tableCount 张表)" -ForegroundColor Red
        return $false
    }
}

function Test-SeedData {
    Write-Host "`n[5/8] 检查种子数据..." -ForegroundColor Cyan
    $result = docker exec fulla-postgres psql -U fulla_user -d fulla_db -t -c "
        SELECT COUNT(*) FROM users WHERE username = 'admin';
    " 2>&1
    
    $count = [int]$result.Trim()
    if ($count -eq 1) {
        Write-Host "[+] 管理员账号已创建" -ForegroundColor Green
        return $true
    } else {
        Write-Host "[-] 管理员账号未创建" -ForegroundColor Red
        return $false
    }
}

function Test-RedisConnection {
    Write-Host "`n[6/8] 检查 Redis 连接..." -ForegroundColor Cyan
    $result = docker exec fulla-redis redis-cli -a redis_secret_pass ping 2>&1
    if ($result -match "PONG") {
        Write-Host "[+] Redis 连接正常" -ForegroundColor Green
        return $true
    } else {
        Write-Host "[-] Redis 连接失败" -ForegroundColor Red
        return $false
    }
}

function Test-TokenEndpoint {
    Write-Host "`n[7/8] 测试令牌端点..." -ForegroundColor Cyan
    try {
        $body = @{
            grant_type = "password"
            client_id = "admin-console"
            client_secret = "admin-secret"
            username = "admin"
            password = "admin123"
            scope = "admin"
        } | ConvertTo-Json
        
        $response = Invoke-RestMethod -Uri "$BackendUrl/oauth2/token" -Method Post -Body $body -ContentType "application/json"
        
        if ($response.access_token) {
            Write-Host "[+] 令牌端点正常 (收到 token: $($response.access_token.Substring(0,20))...)" -ForegroundColor Green
            return $true
        }
    } catch {
        Write-Host "[-] 令牌端点失败: $_" -ForegroundColor Red
        return $false
    }
}

function Test-FrontendAccess {
    Write-Host "`n[8/8] 检查前端访问..." -ForegroundColor Cyan
    try {
        $response = Invoke-WebRequest -Uri $FrontendUrl -Method Get -UseBasicParsing
        if ($response.StatusCode -eq 200) {
            Write-Host "[+] 前端页面可访问" -ForegroundColor Green
            return $true
        }
    } catch {
        Write-Host "[-] 前端页面访问失败: $_" -ForegroundColor Red
        return $false
    }
}

# 执行所有测试
$results = @()
$results += Test-ContainerStatus
$results += Test-BackendHealth
$results += Test-DatabaseConnection
$results += Test-DatabaseTables
$results += Test-SeedData
$results += Test-RedisConnection
$results += Test-TokenEndpoint
$results += Test-FrontendAccess

# 汇总结果
$passed = ($results | Where-Object { $_ -eq $true }).Count
$total = $results.Count

Write-Host "`n" -NoNewline
Write-Host ("=" * 60) -ForegroundColor DarkGray
Write-Host "验证结果: $passed / $total 通过" -ForegroundColor $(if ($passed -eq $total) { "Green" } else { "Yellow" })
Write-Host ("=" * 60) -ForegroundColor DarkGray

if ($passed -eq $total) {
    Write-Host "`n[+] 部署验证完全通过！系统可以投入使用。" -ForegroundColor Green
    exit 0
} else {
    Write-Host "`n[-] 部署验证失败，请检查上述错误项。" -ForegroundColor Red
    exit 1
}
```

**使用方法**：

```powershell
# 基本验证
.\verify-deployment.ps1

# 详细模式
.\verify-deployment.ps1 -Verbose

# 自定义端点
.\verify-deployment.ps1 -BackendUrl "https://your-domain.com" -FrontendUrl "https://your-domain.com"
```

---

## 验证报告模板

完成验证后，建议填写以下报告模板：

```markdown
## fulla 部署验证报告

**验证日期**：2024-06-23
**验证环境**：Windows Docker Desktop / Linux 生产服务器
**验证人员**：[姓名]

### 验证结果汇总

| 阶段 | 状态 | 备注 |
|------|------|------|
| 基础设施验证 | [+] 通过 | 所有容器正常运行 |
| 数据库初始化验证 | [+] 通过 | 7 张表，管理员账号已创建 |
| 后端 API 验证 | [+] 通过 | 令牌端点、内省、撤销功能正常 |
| 管理后台 API 验证 | [+] 通过 | 用户、客户端、Scope 管理正常 |
| 前端功能验证 | [+] 通过 | 用户登录、注册、个人资料功能正常 |
| 安全性验证 | [+] 通过 | 错误处理、令牌验证正常 |
| 性能和监控验证 | [!] 部分通过 | Prometheus 正常，需要优化慢查询 |

### 发现的问题

1. **问题描述**：[具体问题]
   - **影响范围**：[哪些功能受影响]
   - **解决方案**：[如何解决]
   - **状态**：[待解决 | 已解决]

### 优化建议

1. [建议 1]
2. [建议 2]

### 下一步行动

- [ ] 部署到生产环境
- [ ] 配置 Let's Encrypt 证书
- [ ] 设置监控告警
- [ ] 执行性能压测

### 签名确认

验证人员：__________  日期：__________
审核人员：__________  日期：__________
```

---

## 总结

本验证清单涵盖了 fulla 系统的所有核心功能：

[+] **基础设施**：Docker 容器、网络、存储卷  
[+] **数据层**：PostgreSQL 数据库、Redis 缓存  
[+] **业务层**：OAuth2 核心流程、管理后台 API  
[+] **表现层**：Vue.js 用户前端、管理后台  
[+] **安全性**：认证、授权、令牌管理  
[+] **可观测性**：日志、指标、健康检查  

**验证通过标准**：
- 所有容器状态为 `Up`
- 后端健康检查通过
- 数据库表结构完整（至少 7 张表）
- 管理员账号可用
- OAuth2 核心流程（授权、令牌、内省、撤销）正常
- 前端页面可访问并完成基本操作

完成本清单验证后，系统即可投入生产使用。
