# OAuth2 API 接口文档

> **完整 API 规范**: 本项目的完整 API 定义位于 [`OAuth2Server/openapi.yaml`](../../OAuth2Server/openapi.yaml)，可通过 Swagger UI 在线浏览：`/docs/api`。

本服务提供基于 OAuth2.0 标准（RFC 6749）的认证授权服务。

## 端点分类概览 (Endpoint Categories)

| 分类 | 描述 | 前缀 |
|------|------|------|
| **Password Reset** | 密码重置请求与确认（基于邮件验证码） | `/api/password-reset` |
| **Email Verification** | 邮箱验证发送与确认 | `/api/email/verify` |
| **MFA (Multi-Factor Auth)** | TOTP 设置、验证、恢复码管理 | `/api/mfa` |
| **Admin API** | 用户管理、客户端管理、审计日志（需 admin 角色） | `/api/admin` |
| **User Self-Service** | 用户个人资料更新、密码修改、会话管理 | `/api/user` |
| **OIDC Discovery** | OpenID Connect 发现端点与 JWKS | `/.well-known/openid-configuration`, `/oauth2/jwks` |

---

## 1. 授权端点 (Authorization Endpoint)

用于请求用户授权，获取 Authorization Code。

- **URL**: `/oauth2/authorize`
- **Method**: `GET`
- **Access**: 公开 (需登录)

### 请求参数 (Query Parameters)

| 参数名 | 必选 | 描述 | 示例 |
|---|---|---|---|
| `response_type` | 是 | 必须为 `code` | `code` |
| `client_id` | 是 | 客户端 ID | `vue-client` |
| `redirect_uri` | 是 | 回调地址 (需完全匹配) | `http://localhost:5173/callback` |
| `scope` | 否 | 申请的权限范围 | `openid profile` |
| `state` | 建议 | 防止 CSRF 的随机串 | `xyz123` |

### 响应

**成功响应**：
重定向至 `redirect_uri`，并附带 `code` 和 `state`。

```http
HTTP/1.1 302 Found
Location: http://localhost:5173/callback?code=SplxlOBeZQQYbYS6WxSbIA&state=xyz123
```

**错误响应**：
直接返回 JSON 错误或重定向带 error 参数。

```json
{
  "error": "invalid_client",
  "error_description": "Unknown client_id"
}
```

---

## 2. 令牌端点 (Token Endpoint)

用于使用 Authorization Code 换取 Access Token。

- **URL**: `/oauth2/token`
- **Method**: `POST`
- **Access**: 公开 (需 Client 认证)
- **Content-Type**: `application/x-www-form-urlencoded`

### 请求参数 (Form Data)

| 参数名 | 必选 | 描述 | 示例 |
|---|---|---|---|
| `grant_type` | 是 | 必须为 `authorization_code` | `authorization_code` |
| `code` | 是 | 上一步获取的 code | `SplxlOBeZQQYbYS6WxSbIA` |
| `redirect_uri` | 是 | 必须与获取 code 时一致 | `http://localhost:5173/callback` |
| `client_id` | 是 | 客户端 ID | `vue-client` |
| `client_secret` | 是 | 客户端密钥 (用于验证) | `vue-secret` |

### 响应

**成功 (200 OK)**:

```json
{
  "access_token": "2YotnFZFEjr1zCsicMWpAA",
  "token_type": "Bearer",
  "expires_in": 3600,
  "refresh_token": "tGzv3JOkF0XG5Qx2TlKWIA",
  "scope": "openid profile"
}
```

*(注：`refresh_token` 字段当前已支持通过 `grant_type=refresh_token` 换取新的 Access Token。仅 `refresh_token` 的持久化存储（Postgres 后端）在部分配置下为 pass-through，Redis/Memory 后端暂不存储。)*

**失败 (400/401)**:

```json
{
  "error": "invalid_grant",
  "error_description": "Authorization code has expired"
}
```

**失败 (429 Too Many Requests)**:
请求频率过高，触发限流。

```text
Too Many Requests
```

---

## 3. 用户信息端点 (UserInfo Endpoint)

用于验证 Access Token 并获取用户信息。

- **URL**: `/oauth2/userinfo`
- **Method**: `GET`
- **Access**: 受保护 (Bearer Token)

### 请求头 (Headers)

Authorization: `Bearer {access_token}`

### 响应

**成功 (200 OK)**:

```json
{
  "sub": "admin",
  "name": "admin",
  "email": "admin@example.com",
  "picture": "..."
}
```

**失败 (401 Unauthorized)**:

```json
{
  "error": "invalid_token"
}
```

---

## 4. 辅助接口 (Helper Endpoints)

### 登录提交 (Internal)

- **URL**: `/oauth2/login`
- **Method**: `POST`
- **Desc**: 内部使用的表单提交接口，用于 Session 登录并重定向。

### WeChat 登录 (Optional)

- **URL**: `/api/wechat/login`
- **Method**: `POST`
- **Desc**: 处理微信小程序/扫码登录（演示用途）。

### Google 登录回调 (Optional)

- **URL**: `/google/login`
- **Method**: `POST`
- **Desc**: 接收前端传来的 Google Authorization Code，服务端向 Google 换取 Access Token 并调用 UserInfo API，返回过滤后的用户信息（`sub`, `name`, `email`, `picture`）。
- **请求参数**:
  - `code` (required): Google 返回的授权码
- **成功 (200 OK)**:
  ```json
  {"sub": "1234567890", "name": "John Doe", "email": "john@gmail.com", "picture": "..."}
  ```
- **失败 (400/502)**: code 无效或 Google API 不可达。

### 用户注册

- **URL**: `/api/register`
- **Method**: `POST`
- **Content-Type**: `application/x-www-form-urlencoded`
- **限流**: 每IP每分钟最多 5 次，全局每分钟 5000 次（Hodor 插件）

#### 请求参数 (Form Data)

| 参数名 | 必选 | 描述 |
|---|---|---|
| `username` | 是 | 用户名 |
| `password` | 是 | 密码（明文，服务端 SHA256+Salt 存储）|
| `email` | 否 | 邮件地址 |

#### 响应

- **成功 (200 OK)**: `User Registered`
- **失败 (400 Bad Request)**: 缺少用户名或密码
- **失败 (500 Internal Server Error)**: 用户名已存在等

### 管理员 Dashboard (RBAC Protected)

- **URL**: `/api/admin/dashboard`
- **Method**: `GET`
- **Access**: 受保护，需 `admin` 角色（Header: `Authorization: Bearer <token>`）

#### 响应

- **成功 (200 OK)**:
  ```json
  {"message": "Welcome to Admin Dashboard", "status": "success"}
  ```
- **失败 (401)**: Token 无效或缺失
- **失败 (403)**: 用户已登录但不具备 `admin` 角色

---

## 5. 通用错误码

> **单一权威来源（single source of truth）**：本章节 5.1 与 5.2 的表格由后端 `ErrorCatalog`（`OAuth2Plugin/include/oauth2/error/ErrorCatalog.h`）的 `allEntries()` / `allOAuthEntries()` 生成并由自动化测试校验，请勿手工修改表格行。
> 任一不一致（缺失/多余条目、HTTP 状态码或 Error_Category 不匹配）都会导致校验测试失败：`authforge-tests -r ErrorCatalogDoc`。

### 5.1 应用错误码 (Application Error Codes)

业务端点（Application_Endpoint）返回统一的 Error Envelope，其 `error.code` 取值属于下表登记的 Error_Code 集合；`numeric_code` 与 `category` 同样取自下表，HTTP 状态码按 Error_Category（NETWORK 类按 numeric_code 区分 502/504）一致映射。少数面向资源语义的 VALIDATION 码通过条目级显式覆盖保留迁移前的 HTTP 状态码（方案 A / 需求 11.4）：`VALIDATION_RESOURCE_NOT_FOUND` → 404，资源已存在/冲突类（`VALIDATION_RESOURCE_CONFLICT`、`VALIDATION_USERNAME_TAKEN`、`VALIDATION_EMAIL_TAKEN`、`VALIDATION_CREDENTIAL_ALREADY_REGISTERED`）→ 409，`VALIDATION_RATE_LIMITED` → 429；其余 VALIDATION 码仍为 400。

| Error_Code | numeric_code | Error_Category | HTTP Status | 默认信息 (Client_Safe_Message) |
|---|---|---|---|---|
| `NET_CONNECTION_FAILED` | 1001 | NETWORK | 502 | 上游连接失败 |
| `NET_TIMEOUT` | 1002 | NETWORK | 504 | 请求超时 |
| `DB_CONNECTION_ERROR` | 2001 | DATABASE | 500 | 服务暂时不可用 |
| `DB_QUERY_ERROR` | 2002 | DATABASE | 500 | 服务暂时不可用 |
| `DB_CONSTRAINT_VIOLATION` | 2003 | DATABASE | 500 | 数据冲突 |
| `VALIDATION_INVALID_INPUT` | 3001 | VALIDATION | 400 | 输入参数有误 |
| `VALIDATION_MISSING_REQUIRED_FIELD` | 3002 | VALIDATION | 400 | 缺少必填字段 |
| `VALIDATION_FORMAT_ERROR` | 3003 | VALIDATION | 400 | 格式不正确 |
| `VALIDATION_RESOURCE_NOT_FOUND` | 3004 | VALIDATION | 404 | 资源不存在 |
| `VALIDATION_RESOURCE_CONFLICT` | 3005 | VALIDATION | 409 | 资源已存在或冲突 |
| `VALIDATION_USERNAME_TAKEN` | 3006 | VALIDATION | 409 | 该用户名已被注册 |
| `VALIDATION_EMAIL_TAKEN` | 3007 | VALIDATION | 409 | 该邮箱已被注册 |
| `VALIDATION_CREDENTIAL_ALREADY_REGISTERED` | 3008 | VALIDATION | 409 | 该安全密钥已注册，无需重复添加 |
| `VALIDATION_RESET_TOKEN_INVALID` | 3009 | VALIDATION | 400 | 重置链接已失效，请重新申请 |
| `VALIDATION_VERIFICATION_TOKEN_INVALID` | 3010 | VALIDATION | 400 | 验证链接已失效，请重新发送邮件 |
| `VALIDATION_DEVICE_CODE_INVALID` | 3011 | VALIDATION | 400 | 设备码无效、已过期或已被处理 |
| `VALIDATION_RATE_LIMITED` | 3012 | VALIDATION | 429 | 请求过于频繁，请稍后重试 |
| `AUTH_INVALID_CREDENTIALS` | 4001 | AUTHENTICATION | 401 | 用户名或密码错误 |
| `AUTH_TOKEN_EXPIRED` | 4002 | AUTHENTICATION | 401 | 登录已过期 |
| `AUTH_TOKEN_INVALID` | 4003 | AUTHENTICATION | 401 | 登录凭证无效 |
| `AUTH_MFA_CODE_INVALID` | 4004 | AUTHENTICATION | 401 | 验证码不正确 |
| `AUTH_MFA_NOT_CONFIGURED` | 4005 | AUTHENTICATION | 401 | 尚未设置双重验证，请先完成设置 |
| `AUTHZ_ACCESS_DENIED` | 5001 | AUTHORIZATION | 403 | 没有访问权限 |
| `AUTHZ_INSUFFICIENT_PERMISSIONS` | 5002 | AUTHORIZATION | 403 | 权限不足 |
| `INTERNAL_ERROR` | 6001 | INTERNAL | 500 | 服务器内部错误 |

### 5.2 OAuth2 协议错误码 (RFC 6749 §5.2 / RFC 7009 / RFC 8628)

OAuth2 协议端点（OAuth2_Protocol_Endpoint）保持 RFC 6749 §5.2 错误体结构 `{ "error", "error_description", "error_uri" }`，其 `error` 取值与 HTTP 状态码取自下表。

| error | HTTP Status | 默认 error_description |
|---|---|---|
| `invalid_request` | 400 | 请求参数缺失或无效 |
| `invalid_client` | 401 | 客户端认证失败 |
| `invalid_grant` | 400 | 授权许可无效或已过期 |
| `unauthorized_client` | 400 | 客户端无权使用该授权类型 |
| `unsupported_grant_type` | 400 | 不支持的授权类型 |
| `invalid_scope` | 400 | 请求的 scope 无效 |
| `server_error` | 500 | 服务器内部错误 |
| `temporarily_unavailable` | 503 | 服务暂时不可用 |
| `access_denied` | 403 | 授权请求被拒绝（用户无权或拒绝授权） |
| `unsupported_token_type` | 400 | 不支持的令牌类型 |
| `authorization_pending` | 400 | 授权尚未完成，请稍后重试 |
| `slow_down` | 400 | 轮询过于频繁，请降低频率 |
| `expired_token` | 400 | 设备码已过期，请重新发起授权 |

### 5.3 HTTP 状态码速查

| HTTP Status | 描述 | 原因示例 |
|---|---|---|
| `200` | OK | 请求成功 |
| `302` | Found | 重定向 (如 OAuth2 授权跳转) |
| `400` | Bad Request | 参数错误, `invalid_grant`, `unauthorized_client` |
| `401` | Unauthorized | Token 无效或过期, `invalid_client` |
| `403` | Forbidden | **RBAC 拦截**: 用户已登录但缺少所需角色, `access_denied` |
| `429` | Too Many Requests | 触发限流 (Rate Limiting) |
| `500` | Internal Server Error | 服务器内部错误 |

---

## 6. API 文档维护流程

为确保 OpenAPI 文档与代码实现保持同步，本项目采用自动化生成与验证机制。

### 6.1 开发阶段维护

**新增或修改端点时：**
1. 在对应 Controller 的初始化方法中添加或更新 `OpenApiGenerator::addEndpoint()` 调用。
2. 运行项目或测试以生成更新的 `openapi.json`。
3. 验证 Swagger UI (`http://localhost:5555/docs/api/`) 显示正确。
4. 提交代码时必须包含更新后的 `docs/api/openapi.json`。

### 6.2 自动化验证机制

*   **CI 集成检查**：在 GitHub Actions 中会自动运行 `swagger-cli validate` 验证 OpenAPI 规范的合法性。
*   **Pre-commit Hook**：建议在本地配置 Git 钩子。当修改 Controller 文件时，自动运行 `OpenApiGenerator` 测试并检查 `openapi.json` 是否已更新。

### 6.3 质量标准

*   **必需字段**：`path`, `method`, `summary`, `description`, `tags`, `responses`, `requiresAuth`。
*   **推荐做法**：为每个响应码提供 `responseExamples`，并详细定义参数的 `type` 和 `location`。

### 6.4 故障排查

*   **Swagger UI 无法访问**：检查 `docs/api/swagger-ui/` 目录是否存在，确认静态文件服务已启用。
*   **OpenAPI 生成失败**：运行 `authforge-tests -r OpenApiGenerator` 单元测试，查看具体的注册错误。
*   **文档不一致**：确认是否在 Controller 代码变更后重新运行了生成流程，并提交了最新的 `openapi.json`。

