# Drogon OAuth2.0 Provider — 全栈授权服务器

[English](README.md)

![Linux CI](https://github.com/lucaswang420/authforge/actions/workflows/ci-linux.yml/badge.svg)
![Windows CI](https://github.com/lucaswang420/authforge/actions/workflows/ci-windows.yml/badge.svg)
![macOS CI](https://github.com/lucaswang420/authforge/actions/workflows/ci-macos.yml/badge.svg)
![License](https://img.shields.io/badge/license-MIT-blue.svg)

生产级 OAuth2.0/OIDC 授权服务器，完整支持 RFC 6749、RFC 7662、RFC 7009、RFC 8414 标准。包含管理后台、前端客户端和完整的测试体系。

---

## 项目架构

```
authforge/
├── apps/server/        # 授权服务器后端（Drogon C++ 框架）
├── libs/               # SDK 库包（authforge::common/oauth2/identity/storage-*/drogon）
├── frontends/admin/    # 管理后台前端（Vue 3 + TailwindCSS）
├── frontends/user/     # 用户端前端（Vue 3 + Pinia + TailwindCSS）
├── scripts/            # 构建、测试、运维脚本
├── docs/               # 项目文档
└── PRD/                # 产品设计文档
```

### 技术栈

| 层级 | 技术 |
|------|------|
| 后端框架 | Drogon (C++17) |
| 数据库 | PostgreSQL 14+ |
| 缓存 | Redis 7+ |
| 管理后台 | Vue 3 + Vite + Pinia + TailwindCSS |
| 用户前端 | Vue 3 + Vite |
| 测试 | CTest (C++) + Playwright (E2E) + PowerShell (API) |
| 监控 | Prometheus + 审计日志 |
| 部署 | Docker Compose / Nginx |

---

## 功能模块

### OAuth2/OIDC 核心协议

| 功能 | 标准 | 端点 |
|------|------|------|
| 授权码流程 + PKCE | RFC 6749 / RFC 7636 | `/oauth2/authorize`、`/oauth2/login`、`/oauth2/token` |
| 客户端凭证模式 | RFC 6749 | `/oauth2/token` (grant_type=client_credentials) |
| Token 刷新 | RFC 6749 | `/oauth2/token` (grant_type=refresh_token) |
| Token 内省 | RFC 7662 | `/oauth2/introspect` |
| Token 撤销 | RFC 7009 | `/oauth2/revoke` |
| OIDC Discovery | RFC 8414 | `/.well-known/openid-configuration` |
| JWKS | RFC 7517 | `/.well-known/jwks.json` |
| UserInfo | OIDC Core | `/oauth2/userinfo` |
| 用户同意 | OAuth2 | `/oauth2/consent` |
| 设备授权流 | RFC 8628 | `/oauth2/device_authorization` |
| 动态客户端注册 | RFC 7591 | `/oauth2/register` |

### 用户认证与安全

| 功能 | 端点 |
|------|------|
| 用户注册 | `POST /api/register` |
| 密码重置 | `/api/password-reset/request`、`/api/password-reset/confirm` |
| 邮箱验证 | `/api/verify-email`、`/api/verify-email/resend` |
| MFA (TOTP) | `/api/me/mfa/setup`、`/api/me/mfa/verify`、`/api/me/mfa/disable` |
| WebAuthn (FIDO2) | `/api/me/webauthn/register/*`、`/oauth2/webauthn/authenticate/*` |
| 外部登录 (Google) | `/api/google/login` |
| 外部登录 (微信) | `/api/wechat/login` |
| 账号锁定保护 | 渐进式锁定（5/10/15/20次失败递增） |

### 用户自助服务

| 功能 | 端点 |
|------|------|
| 个人资料 | `GET /api/me` |
| 修改密码 | `PUT /api/me/password` |
| 已授权应用管理 | `GET/DELETE /api/me/authorized-apps` |
| 注销账号 | `DELETE /api/me` |

### Admin 管理后台 (frontends/admin)

| 模块 | 功能 |
|------|------|
| 仪表盘 | 用户数、应用数、活跃Token数、失败登录统计 |
| 应用管理 | Client CRUD、Secret 重置、Scope 分配、Grant Type 配置 |
| 用户管理 | 用户列表/详情、角色分配、禁用/启用、锁定状态查看 |
| 角色管理 | 角色 CRUD（保护内置角色 admin/user） |
| Scope 管理 | Scope CRUD（保护内置 Scope openid/profile/email/admin） |
| Token 管理 | Token 列表、按客户端/用户撤销、单个撤销 |
| 组织管理 | 多租户组织 CRUD |
| 审计日志 | 分页查看、按事件类型/结果筛选 |
| OIDC 密钥 | 签名密钥信息查看 |
| 系统设置 | 健康状态监控 |

### RBAC 权限系统

- 基于角色的访问控制（admin / user / 自定义角色）
- URL 模式匹配的权限检查（`/api/admin/.*` → admin 角色）
- 三重 Scope 权限控制（Client 限制 + Role 校验 + Consent 检查）

### 可观测性

- Prometheus 指标导出 (`/metrics`)
- 结构化审计日志（登录、Token 签发/撤销、密码变更等）
- 健康检查端点 (`/health`、`/health/live`、`/health/ready`)

---

## 快速开始

### Docker Compose（推荐）

```bash
docker-compose up -d
```

- 用户前端：`http://localhost:8080`
- 管理后台：`http://localhost:5174/admin/`
- 后端 API：`http://localhost:5555`

### 本地开发

```powershell
# 1. 编译后端
.\manage.ps1 build-backend

# 2. 启动后端（需要 PostgreSQL + Redis）
cd apps\server
..\..\build\apps\server\Debug\authforge-server.exe

# 3. 启动管理后台
cd frontends\admin
npm install
npm run dev    # http://localhost:5174/admin/

# 4. 启动用户前端
cd frontends\user
npm install
npm run dev    # http://localhost:5173
```

### 默认账号

| 用户名 | 密码 | 角色 |
|--------|------|------|
| admin | admin | admin |

---

## 测试

### 后端 API 测试

```powershell
# Admin API 全量测试（37 tests）
.\scripts\backend\test-admin-endpoints.ps1

# OAuth2 核心流程测试（17 tests）
.\scripts\backend\test-oauth2-endpoints.ps1
```

### 前端 E2E 测试

```powershell
cd frontends\admin
npx playwright test              # 全量运行（123 tests）
npx playwright test --ui         # UI 模式调试
npx playwright test --headed     # 有头浏览器模式
```

### C++ 单元测试

```powershell
cd build
ctest --output-on-failure
```

### 测试覆盖统计

| 测试类型 | 数量 | 覆盖范围 |
|----------|------|----------|
| Admin API (PowerShell) | 37 | 全部 Admin 端点 + Organization |
| OAuth2 Core (PowerShell) | 17 | 认证流程、Token 管理、用户服务 |
| 前端 E2E (Playwright) | 123 | 管理后台所有页面和交互 |
| C++ 单元测试 (CTest) | 111 | 核心库逻辑 |

---

## API 文档

- **OpenAPI 规范**：[openapi.yaml](apps/server/openapi.yaml)
- **Swagger UI**：`http://localhost:5555/docs/api`（需部署 Swagger UI 静态文件）
- **E2E 测试指南**：[E2E_TESTING_GUIDE.md](docs/admin/e2e-testing-guide.md)

---

## 项目文档

| 文档 | 说明 |
|------|------|
| [后端配置指南](docs/backend/configuration-guide.md) | 数据库、Redis、环境变量配置 |
| [安全架构](docs/backend/security-architecture.md) | Token 生命周期、加密、防护策略 |
| [RBAC 权限](docs/backend/rbac-guide.md) | 角色权限配置说明 |
| [Docker 部署](docs/backend/docker-deployment.md) | 容器化部署方案 |
| [CI/CD 流水线](docs/backend/ci-cd-guide.md) | GitHub Actions 配置 |
| [Admin Console 设计](PRD/admin_console_design.md) | 管理后台产品设计文档 |
| [账号锁定机制](docs/ops/account-lockout.md) | 锁定规则和重置方法 |

---

## 系统要求

| 组件 | 最低版本 |
|------|----------|
| C++ 编译器 | C++17 (MSVC 2019+ / GCC 9+ / Clang 10+) |
| CMake | 3.20+ |
| PostgreSQL | 14+ |
| Redis | 7+ |
| Node.js | 18+ |
| Docker | 24+（可选） |

---

## 许可证

MIT License — 详见 [LICENSE](LICENSE)

---

**项目状态**：生产就绪 | **版本**：v6.0.0
