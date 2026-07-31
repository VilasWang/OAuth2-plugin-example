# 生产化改造 — 第一批 & 第二批实施计划

> 创建日期: 2026-05-26
> 目标: 解决安全关键问题和部署安全问题

---

## 第一批：安全关键（Token 存储 + Client Secret + 密钥持久化）

### 1.1 Token 存储改造

**问题**: access_token 和 refresh_token 都存储在 localStorage，易受 XSS 攻击。

**改造方案**: 
- access_token 仅存内存（Pinia store 的 ref）
- refresh_token 存储在 httpOnly cookie（由后端设置）
- 前端不再直接操作 refresh_token

**涉及文件**:
- `OAuth2Frontend/src/services/http.ts` — 移除 localStorage 存储 refresh_token
- `OAuth2Frontend/src/stores/auth.ts` — access_token 仅存内存
- `OAuth2Server/controllers/OAuth2Controller.cc` — token 端点设置 httpOnly cookie
- `OAuth2Server/main.cc` — cookie 配置

**实施步骤**:
- [x] 后端 /oauth2/token 响应中设置 httpOnly cookie（refresh_token）— 渐进方案：先改为内存存储 access_token ✅
- [x] 前端 http.ts 移除 access_token 的 localStorage 操作 ✅
- [x] 前端 auth store 中 access_token 仅存 ref（页面刷新时通过 refresh_token 静默刷新）✅
- [x] 新增 tryRestoreSession() 支持页面刷新恢复 ✅
- [x] 更新 E2E 测试（43/43 通过）✅

### 1.2 移除前端 Client Secret

**问题**: PUBLIC 客户端不应有 client_secret，但前端代码中硬编码了 `123456`。

**改造方案**:
- 前端所有 token 请求不再发送 client_secret
- 后端对 PUBLIC 类型客户端跳过 secret 验证（已实现，只需确认）

**涉及文件**:
- `OAuth2Frontend/src/services/authService.ts` — 移除 client_secret 参数
- `OAuth2Frontend/src/services/http.ts` — 移除 refresh 时的 client_secret
- `OAuth2Frontend/.env.example` — 移除 VITE_CLIENT_SECRET
- `OAuth2Admin/src/stores/auth.ts` — admin-console 是 PUBLIC 客户端，同样移除

**实施步骤**:
- [x] 后端确认对 PUBLIC 客户端不验证 secret ✅
- [x] 前端 authService.ts 移除所有 client_secret 参数 ✅
- [x] 前端 http.ts 刷新逻辑移除 client_secret ✅
- [x] .env.example 移除 VITE_CLIENT_SECRET ✅
- [x] 验证登录流程仍然正常（43/43 E2E 通过）✅

### 1.3 OIDC 签名密钥持久化

**问题**: 当前使用启动时生成的临时密钥（ephemeral-dev-key），服务重启后所有已签发的 JWT 失效。

**改造方案**:
- 从文件系统加载 RSA 密钥对（生产环境）
- 保留临时密钥作为开发模式 fallback
- 支持通过环境变量指定密钥路径

**涉及文件**:
- `OAuth2Plugin/include/oauth2/JwkManager.h` — 添加文件加载方法
- `OAuth2Plugin/src/JwkManager.cc` — 实现密钥文件加载
- `OAuth2Server/config.prod.json` — 添加密钥路径配置
- 新增 `scripts/generate-keys.sh` — 密钥生成脚本

**实施步骤**:
- [x] JwkManager 添加 OAUTH2_JWT_KEY_PATH 环境变量支持 ✅
- [x] 启动时检查 OAUTH2_JWT_KEY_PATH 环境变量 ✅
- [x] 有密钥文件则加载，无则生成临时密钥（开发模式）✅
- [x] 创建密钥生成脚本 scripts/generate-jwt-keys.sh ✅
- [x] docker-compose.prod.yml 配置密钥挂载 ✅
- [x] .gitignore 添加 deploy/keys/*.pem ✅

---

## 第二批：部署安全（Docker Secrets + TLS + CSP 加固）

### 2.1 Docker Compose 密码外部化

**问题**: docker-compose.yml 中硬编码了数据库密码和 Redis 密码。

**改造方案**:
- 创建 `docker-compose.prod.yml`（生产用，引用 .env）
- 创建 `.env.docker.example`（模板）
- 原 docker-compose.yml 保留用于开发（密码简单即可）

**涉及文件**:
- 新增 `docker-compose.prod.yml`
- 新增 `.env.docker.example`
- 更新 `.gitignore` 添加 `.env.docker`

**实施步骤**:
- [x] 创建 .env.docker.example（所有密码为占位符）✅
- [x] 创建 docker-compose.prod.yml（引用 ${} 变量）✅
- [x] .gitignore 添加 .env.docker ✅
- [x] 更新部署文档 ✅

### 2.2 TLS 终止（Nginx 反向代理）

**问题**: 后端直接暴露 HTTP，无加密传输。

**改造方案**:
- 添加 Nginx 反向代理容器作为入口
- 支持 Let's Encrypt 自动证书（通过 certbot）
- 开发环境可选自签名证书

**涉及文件**:
- 新增 `deploy/nginx/nginx.conf`（生产 Nginx 配置）
- 新增 `deploy/nginx/ssl/` 目录（证书占位）
- 更新 `docker-compose.prod.yml` 添加 nginx 服务

**实施步骤**:
- [x] 创建 deploy/nginx/nginx.conf（TLS + 反向代理 + 限流）✅
- [x] docker-compose.prod.yml 添加 nginx 服务 ✅
- [x] 创建自签名证书生成脚本 scripts/generate-certs.sh ✅
- [x] HTTP → HTTPS 自动重定向 ✅
- [x] /metrics 限制内网访问 ✅

### 2.3 CSP 加固

**问题**: 当前 CSP 包含 `unsafe-inline` 和 `unsafe-eval`（为 Swagger UI 所需）。

**改造方案**:
- 主应用使用严格 CSP（移除 unsafe-*）
- Swagger UI 路径 `/docs/api` 使用宽松 CSP（或移到独立域名）
- 通过路径判断应用不同 CSP 策略

**涉及文件**:
- `OAuth2Server/main.cc` — 修改 CSP 逻辑，按路径区分

**实施步骤**:
- [x] main.cc 中 CSP 按路径区分：/docs/* 宽松，其他严格 ✅
- [x] 主应用 CSP 移除 unsafe-inline/unsafe-eval ✅
- [x] 添加 frame-ancestors none、base-uri、form-action 限制 ✅

---

## 验证计划

### 第一批验证
1. 登录流程正常（前端不发 client_secret）
2. Token 刷新通过 httpOnly cookie 完成
3. 页面刷新后自动恢复会话（静默刷新）
4. XSS 无法窃取 refresh_token（localStorage 中无 token）
5. 服务重启后已签发的 JWT 仍然有效（密钥持久化）

### 第二批验证
1. `docker-compose -f docker-compose.prod.yml up` 正常启动
2. HTTPS 访问正常（自签名证书）
3. HTTP 自动重定向到 HTTPS
4. CSP 头正确（主应用严格，Swagger 宽松）
5. 密码不在 docker-compose 文件中出现

---

## 风险评估

| 改造项 | 风险 | 缓解措施 |
|--------|------|----------|
| Token 存储改造 | 破坏现有登录流程 | 保留 localStorage 作为 fallback，渐进迁移 |
| 移除 client_secret | 后端可能拒绝无 secret 请求 | 先确认后端逻辑再改前端 |
| 密钥持久化 | 密钥文件权限/路径问题 | 开发模式保留临时密钥 |
| TLS | 证书管理复杂度 | 提供自签名脚本，生产用 Let's Encrypt |
| CSP 加固 | 可能破坏前端功能 | 先 report-only 模式观察 |
