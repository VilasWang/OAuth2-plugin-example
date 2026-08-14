---
name: openapi-update
description: 当OAuth2端点发生变化时更新OpenAPI规范
---

# OpenAPI规范更新技能

这个技能帮助您在OAuth2控制器端点发生变化时更新OpenAPI 3.0规范文档。

## 使用方法

- Claude自动调用：当检测到OAuth2Controller.cc或WeChatController.cc中的路由变更时
- 用户调用：`/openapi-update`

## 工作流程

1. **分析当前控制器**
   所有 HTTP 控制器源码位于 `libs/drogon/src/controllers/*.cc`（头文件 `libs/drogon/include/authforge/drogon/controllers/*.h`）。路由注册入口在 `apps/server/src/bootstrap/ControllerRegistration.cc`（Drogon `HttpController<T,false>` 为进程级单例，路由在此显式注册）。至少读取以下与 OAuth2 / Admin API 相关的控制器：
   - `AuthorizationEndpointController.cc` — `/oauth2/authorize`
   - `TokenEndpointController.cc` — `/oauth2/token`、`/oauth2/userinfo`、`/oauth2/introspect`、`/oauth2/revoke`
   - `SessionController.cc` — `/oauth2/login`、`/oauth2/consent`、`/oauth2/end_session`、`/login`、`/api/register`
   - `ClientRegistrationController.cc` — `/oauth2/register`
   - `DiscoveryController.cc` — `/.well-known/openid-configuration`、`/.well-known/jwks.json`
   - `MfaController.cc` — `/oauth2/mfa/*`
   - `DeviceAuthController.cc` — `/oauth2/device_*`
   - `WebAuthnController.cc` — `/oauth2/webauthn/*`、`/api/me/webauthn/*`
   - `UserSelfServiceController.cc` — `/api/me*`
   - `EmailVerificationController.cc` — `/api/verify-email*`
   - `PasswordResetController.cc` — `/api/password-reset/*`
   - `HealthController.cc` — `/health`、`/health/live`、`/health/ready`
   - `ApiDocController.cc` — `/docs/api/openapi.json`、`/docs/api/`
   - `WeChatController.cc`（`#ifdef WITH_SOCIAL`）— `POST /api/wechat/login`
   - `GoogleController.cc` / `GitHubController.cc`（`WITH_SOCIAL`）— `/api/google/login`、`/api/github/login`
   - Admin 控制器：`ClientAdminController.cc`（`/api/admin/clients*`）、`UserAdminController.cc`（`/api/admin/users*`）、`RoleScopeAdminController.cc`（`/api/admin/roles*`、`/api/admin/scopes*`）、`TokenAdminController.cc`（`/api/admin/tokens*`、`/api/admin/oidc/keys`）、`AuditController.cc`（`/api/admin/dashboard*`、`/api/admin/logs`）
   - 另：`apps/server/src/organization/OrganizationController.cc`（产品级组织控制器）
   - 识别所有路由端点和参数（以各控制器头文件的 `ADD_METHOD_TO` 宏为权威来源）

2. **比较现有OpenAPI规范**
   - 读取`apps/server/openapi.yaml`（手工维护的源文档，当前 `info.version: 1.0.0`）
   - 运行时由 `apps/server/src/bootstrap/OpenApiSetup.cc` 生成 JSON 规范到 `apps/server/docs/api/openapi.json`，并由 `ApiDocController` 在 `/docs/api/openapi.json` 与 `/docs/api/` 提供 Swagger UI。**修改 `openapi.yaml` 后需重新运行服务器（或构建）以再生 `openapi.json`**，否则 Swagger UI 与校验脚本看到的仍是旧 JSON。
   - 检查是否有新的端点、参数变更、响应格式变更

3. **更新OpenAPI规范**
   - 添加新的端点定义
   - 更新现有端点的参数
   - 更新响应模型
   - 确保符合OpenAPI 3.0规范

4. **验证规范**
   - 使用验证脚本检查YAML语法
   - 验证所有引用是否有效
   - 确保端点路径与代码一致

### 验证脚本集成

`scripts/backend/validate-openapi.sh` 真实行为：**不接收文件路径参数**（传入的 `$1` 被忽略），脚本内部通过 `SEARCH_PATHS` 查找生成的 `openapi.json`；它会先 `build.sh --debug` 构建、再跑 `ctest`、最后用 `jq` / `python3 -m json.tool` 校验生成的 `openapi.json` 的合法性及必需字段（`openapi` / `info` / `paths` / `servers`）。它**不校验 `openapi.yaml`**，也**不依赖 swagger-cli / spectral**。

```bash
# 正确用法（从仓库根目录，无需参数；会构建并校验生成的 openapi.json）
scripts/backend/validate-openapi.sh

# Windows 上没有 .bat 版本，请用 WSL / Git Bash 运行上面的 .sh，
# 或手动校验生成的 JSON：
jq empty apps/server/docs/api/openapi.json && echo "✅ openapi.json valid"
```

**Windows PowerShell 快速字段检查（针对手工维护的 yaml，仅供参考，非权威校验）**:
```powershell
try {
    $yaml = Get-Content "apps/server/openapi.yaml" -Raw
    Write-Host "✅ YAML file readable"
} catch {
    Write-Host "❌ YAML read error: $_"
    exit 1
}
$requiredFields = @("openapi", "info", "paths", "components")
foreach ($field in $requiredFields) {
    if ($yaml -match "^$field:") {
        Write-Host "✅ Field '$field' found"
    } else {
        Write-Host "❌ Required field '$field' missing"
        exit 1
    }
}
```

## 需要检查的关键端点

> 以控制器头文件 `ADD_METHOD_TO` 宏为权威。以下为当前实际路由（方法 + 路径）。

### OAuth2 标准端点
- `GET /oauth2/authorize` - 授权端点
- `POST /oauth2/token` - 令牌端点（授权码 / 刷新 / 客户端凭证）
- `POST /oauth2/revoke` - 撤销端点
- `POST /oauth2/login` - 登录（获取授权码）
- `GET /oauth2/userinfo` - 用户信息
- `POST /oauth2/introspect` - 令牌内省（RFC 7662）
- `POST /oauth2/consent` - 授权同意
- `GET|POST /oauth2/end_session` - 注销
- `POST /oauth2/register` - 动态客户端注册（RFC 7591）
- MFA：`/oauth2/mfa/setup`、`/oauth2/mfa/setup/verify`、`/oauth2/mfa/disable`、`/oauth2/mfa/verify`
- Device：`/oauth2/device_authorization`、`/oauth2/device/verify`、`/oauth2/device/approve`
- WebAuthn：`/oauth2/webauthn/authenticate/begin`、`/oauth2/webauthn/authenticate/finish`
- 发现：`/.well-known/openid-configuration`、`/.well-known/jwks.json`
- 健康检查：`/health`、`/health/live`、`/health/ready`

### 社交登录端点（`#ifdef WITH_SOCIAL`）
- `POST /api/wechat/login` - 微信登录（**POST，非 GET**；无独立 `/api/wechat/callback` 路由）
- `POST /api/google/login` - Google 登录
- `POST /api/github/login` - GitHub 登录

### Admin 管理端点（均需 AuthorizationFilter）
- `GET /api/admin/dashboard`、`GET /api/admin/dashboard/stats`、`GET /api/admin/logs`
- `GET|POST /api/admin/users`、`GET|PUT /api/admin/users/{userId}`、`PUT /api/admin/users/{userId}/disable`、`POST /api/admin/users/{userId}/enable`、`GET|PUT /api/admin/users/{userId}/roles`（**无 DELETE**）
- `GET|POST /api/admin/clients`、`GET|PUT|DELETE /api/admin/clients/{clientId}`、`POST /api/admin/clients/{clientId}/reset-secret`、`GET|PUT /api/admin/clients/{clientId}/scopes`
- `GET|POST /api/admin/roles`、`PUT|DELETE /api/admin/roles/{roleId}`
- `GET|POST /api/admin/scopes`、`PUT|DELETE /api/admin/scopes/{scopeId}`、`GET /api/admin/scopes/resources`
- `GET /api/admin/tokens`、`POST /api/admin/tokens/revoke-by-client`、`POST /api/admin/tokens/revoke-by-user`、`DELETE /api/admin/tokens/{tokenPrefix}`
- `GET /api/admin/oidc/keys`

## 输出格式

更新后的`openapi.yaml`文件应包含：
- 正确的OpenAPI 3.0版本
- 所有端点的完整文档
- 请求参数 schema
- 响应格式定义
- 错误响应示例
- 认证方式说明

## 注意事项

- 保持YAML缩进一致（2个空格）
- 所有端点需要包含描述文字
- 参数需要标注是否必需
- 提供请求和响应示例
- 更新版本号当有重大变更

## 版本控制集成

```bash
# 更新规范后提交到 Git
git add apps/server/openapi.yaml
git commit -m "docs: update OpenAPI specification for endpoint changes"

# 如果有重大变更，更新 API 版本号
# 在 openapi.yaml 的 info.version 字段中递增版本
```

## 文档同步

```bash
# 确保相关文档也同步更新
# - docs/api_reference.md
# - README.md 中的 API 端点示例
# - 技术文档中的接口描述
```
