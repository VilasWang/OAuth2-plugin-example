# OAuth2 Admin Console Phase 5 — 功能补全设计

> 版本: v1.0
> 创建日期: 2026-05-21
> 前置: Phase 1-4 已完成（脚手架/CRUD/审计日志/Docker部署/E2E测试）

---

## 一、背景与目标

Phase 1-4 实现了基础管理功能，但以下核心能力缺失：

1. **应用详情页** — 无法查看/编辑单个应用的完整配置
2. **应用 Scope 分配** — 无法管理应用允许请求的 scopes（影响 client_credentials 模式）
3. **Token 管理** — 无法查看/撤销活跃令牌
4. **OIDC 签名密钥** — 无法查看当前 JWK 公钥信息

本阶段目标：补全这些功能，使 Admin Console 成为完整的 OAuth2 + OIDC 管理中心。

---

## 二、功能设计

### 2.1 应用详情页 (`/admin/applications/:id`)

从应用列表点击进入，展示完整配置，支持编辑。

**Tab 结构**：

| Tab | 内容 | 可编辑 |
|-----|------|--------|
| 基本信息 | name, client_id, client_type, created_at | name |
| 认证配置 | redirect_uris, allowed_grant_types | 全部 |
| Scope 权限 | 已分配 scopes（checkbox 列表） | 全部 |
| Token 设置 | access_token_ttl, refresh_token_ttl | 全部 |
| 凭据 | client_secret 重置 | 重置操作 |

**交互**：

- 编辑后点"Save"提交，Toast 反馈
- redirect_uris 支持多行输入（每行一个 URI）
- grant_types 使用 checkbox 组（同创建页面）
- Scope 权限使用 checkbox 列表，从 `/api/admin/scopes` 获取全量 scope

**Client Credentials 模式特殊说明**：

- 当 grant_types 包含 `client_credentials` 时，Scope 权限 Tab 显示提示：
  > "这些 Scope 决定了该应用通过 Client Credentials 模式获取的 Token 权限范围"
- 无 redirect_uris 要求（可为空）

### 2.2 Token 管理页 (`/admin/tokens`)

新增侧边栏导航项，展示系统中活跃的令牌。

**列表字段**：

| 字段 | 说明 |
|------|------|
| Token (前8位) | 脱敏显示 |
| 类型 | access / refresh |
| 所属应用 | client_id |
| 所属用户 | username（client_credentials 模式为空） |
| Scope | 授权的 scope 列表 |
| 签发时间 | created_at |
| 过期时间 | expires_at |
| 状态 | active / expired / revoked |

**操作**：

- **撤销单个 Token**：点击行内"Revoke"按钮，二次确认
- **批量撤销**：按应用或按用户撤销所有 token
- **筛选**：按应用、按用户、按状态筛选

**分页**：服务端分页，每页 50 条，仅显示未过期的 token（默认）

### 2.3 OIDC 签名密钥查看 (`/admin/settings` 扩展)

在 Settings 页面新增"签名密钥"区域：

**展示内容**：

- 当前 JWK 公钥信息（kid, kty, alg, use）
- 公钥 PEM 格式（可复制）
- JWKS 端点 URL（`/.well-known/jwks.json`）
- Discovery 端点 URL（`/.well-known/openid-configuration`）

**交互**：

- 只读展示（密钥轮转为低优先级，暂不实现）
- 提供"复制 JWKS URL"按钮

---

## 三、后端 API 设计

### 3.1 需要新增的 API

| 接口 | 方法 | 用途 | 请求/响应 |
|------|------|------|-----------|
| `/api/admin/clients/:id` | GET | 获取应用详情 | 返回完整 client 信息 + scopes |
| `/api/admin/clients/:id` | PUT | 更新应用配置 | body: name, redirect_uris, allowed_grant_types |
| `/api/admin/clients/:id/scopes` | GET | 获取应用已分配 scopes | 返回 scope name 数组 |
| `/api/admin/clients/:id/scopes` | PUT | 更新应用 scopes | body: { scopes: ["openid", "profile"] } |
| `/api/admin/tokens` | GET | 活跃令牌列表 | query: client_id, user_id, page, per_page |
| `/api/admin/tokens/:token_prefix` | DELETE | 撤销令牌 | 按 token 前缀匹配删除 |
| `/api/admin/tokens/revoke-by-client` | POST | 按应用撤销 | body: { client_id } |
| `/api/admin/tokens/revoke-by-user` | POST | 按用户撤销 | body: { user_id } |
| `/api/admin/oidc/keys` | GET | 获取当前 JWK 信息 | 返回 kid, alg, public_key_pem |

### 3.2 API 响应格式

**GET /api/admin/clients/:id**：

```json
{
  "client_id": "vue-client",
  "name": "Vue Frontend",
  "client_type": "PUBLIC",
  "redirect_uris": "http://localhost:5173/callback",
  "allowed_grant_types": "authorization_code,refresh_token",
  "created_at": "2026-05-20T10:00:00Z",
  "scopes": ["openid", "profile", "email"]
}
```

**GET /api/admin/tokens**：

```json
{
  "tokens": [
    {
      "token_prefix": "a1b2c3d4",
      "type": "access",
      "client_id": "vue-client",
      "username": "admin",
      "scope": "openid profile",
      "created_at": "2026-05-21T10:00:00Z",
      "expires_at": "2026-05-21T11:00:00Z"
    }
  ],
  "total": 42,
  "page": 1,
  "per_page": 50
}
```

**GET /api/admin/oidc/keys**：

```json
{
  "kid": "default-key-1",
  "kty": "RSA",
  "alg": "RS256",
  "use": "sig",
  "public_key_pem": "-----BEGIN PUBLIC KEY-----\n...\n-----END PUBLIC KEY-----",
  "jwks_uri": "http://localhost:5555/.well-known/jwks.json",
  "discovery_uri": "http://localhost:5555/.well-known/openid-configuration"
}
```

---

## 四、前端页面设计

### 4.1 应用详情页布局

```text
┌─────────────────────────────────────────────────┐
│ ← Back to Applications    [Save Changes]        │
├─────────────────────────────────────────────────┤
│ Tab: Info | Auth | Scopes | Tokens | Credentials│
├─────────────────────────────────────────────────┤
│                                                 │
│  [Tab Content Area]                             │
│                                                 │
│  Info Tab:                                      │
│    Client ID: vue-client  [copy]                │
│    Name: [input field]                          │
│    Type: PUBLIC (readonly)                      │
│    Created: 2026-05-20                          │
│                                                 │
│  Scopes Tab:                                    │
│    ☑ openid    - OpenID Connect                 │
│    ☑ profile   - User profile                   │
│    ☐ email     - Email address                  │
│    ☐ admin     - Admin access (requires admin)  │
│    [Save Scopes]                                │
│                                                 │
└─────────────────────────────────────────────────┘
```

### 4.2 Token 管理页布局

```text
┌─────────────────────────────────────────────────┐
│ Tokens                    [Revoke by App ▾]     │
├─────────────────────────────────────────────────┤
│ Filter: [App ▾] [User ▾] [Status ▾]            │
├─────────────────────────────────────────────────┤
│ Token    | Type   | App       | User   | Exp   │
│ a1b2c3.. | access | vue-client| admin  | 1h    │
│ d4e5f6.. | refresh| api-svc   | —      | 7d    │
│          |        |           |        | [Rev] │
├─────────────────────────────────────────────────┤
│ ← Previous    Page 1    Next →                  │
└─────────────────────────────────────────────────┘
```

### 4.3 路由变更

| 路由 | 组件 | 说明 |
|------|------|------|
| `/admin/applications/:id` | ApplicationDetailPage.vue | 新增 |
| `/admin/tokens` | TokensPage.vue | 新增 |

侧边栏新增 "Tokens" 导航项（位于 Audit Logs 之后）。

---

## 五、实施计划

### Phase 5A: 应用详情页 + Scope 分配（优先级最高）

**后端**：

1. 新增 `GET /api/admin/clients/:id` — 返回单个 client 详情 + scopes
2. 新增 `PUT /api/admin/clients/:id` — 更新 client 配置（name, redirect_uris, grant_types）
3. 新增 `GET /api/admin/clients/:id/scopes` — 获取 client 已分配 scopes
4. 新增 `PUT /api/admin/clients/:id/scopes` — 更新 client scopes

**前端**：

5. 创建 `ApplicationDetailPage.vue` — Tab 式详情页
6. 添加路由 `/admin/applications/:id`
7. 应用列表行点击跳转到详情页
8. Scope 分配 Tab — checkbox 列表 + 保存

### Phase 5B: Token 管理（优先级中）

**后端**：

9. 新增 `GET /api/admin/tokens` — 活跃令牌列表（分页 + 筛选）
10. 新增 `DELETE /api/admin/tokens/:prefix` — 撤销单个令牌
11. 新增 `POST /api/admin/tokens/revoke-by-client` — 按应用批量撤销
12. 新增 `POST /api/admin/tokens/revoke-by-user` — 按用户批量撤销

**前端**：

13. 创建 `TokensPage.vue` — 令牌列表 + 筛选 + 撤销
14. 侧边栏添加 Tokens 导航项
15. 添加路由 `/admin/tokens`

### Phase 5C: OIDC 密钥查看（优先级低）

**后端**：

16. 新增 `GET /api/admin/oidc/keys` — 返回当前 JWK 公钥信息

**前端**：

17. Settings 页面新增"签名密钥"区域
18. 展示 JWK 信息 + JWKS/Discovery URL

### Phase 5D: E2E 测试补充

19. 应用详情页 E2E 测试（查看/编辑/scope 分配）
20. Token 管理页 E2E 测试（列表/撤销/筛选）
21. Settings OIDC 密钥展示测试

---

## 六、依赖关系

```text
Phase 5A (1-8)  ← 无外部依赖，可立即开始
Phase 5B (9-15) ← 依赖 oauth2_access_tokens 表结构（已存在）
Phase 5C (16-18) ← 依赖 JwkManager（已实现）
Phase 5D (19-21) ← 依赖 5A/5B/5C 完成
```

---

## 七、验收标准

1. 应用列表点击可进入详情页，所有字段正确展示
2. 编辑 redirect_uris / grant_types 后保存成功
3. Scope 分配 checkbox 正确反映当前状态，修改后保存生效
4. Client Credentials 应用的 scope 决定 token 权限（可通过 token introspection 验证）
5. Token 列表正确展示活跃令牌，撤销后 token 立即失效
6. Settings 页面展示 JWK 公钥信息和端点 URL
7. 所有新功能有对应 E2E 测试覆盖
