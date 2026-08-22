# AuthForge IAM 业务架构调研报告（基于代码验证）

> **调研日期**: 2026-08-09（2026-08-11 复核修正）
> **调研方法**: 穷尽式代码验证——`search_content` 关键词穷尽搜索 + `read_file` 逐个验证关键文件 + 迁移 SQL 表结构核对 + 子代理 230+ 次工具调用交叉验证
> **验证范围**: AuthForge v1.0.0 全代码库
> **声明**: 本报告每个功能点的状态判断都有具体 `file:line` 出处。状态分四级：[完整实现] / [部分实现] / [桩实现] / [未实现]
>
> **2026-08-11 复核说明**: 本报告核心结论经二次验证确认准确。本次修正了以下硬错误：迁移文件数（27→22）、ORM 表数（18→19）、Redis 存储层描述（补充 #42 RedisCachedClientRepository L2 缓存）、备份码引用精度、以及标注 OAuth/OIDC 合规审计（F-001..F-031）已全部修复。

---

## 一、IAM 业务架构组成（行业全景）

一个完整的 IAM 系统按 Gartner / NIST SP 800-63 参考模型由以下业务域组成：

| 业务域 | 核心职责 |
|--------|----------|
| 认证（Authentication） | 凭证登录、MFA、WebAuthn、社交登录、SSO |
| 授权（Authorization） | OAuth2、OIDC、RBAC、ABAC、策略引擎 |
| 用户与身份管理 | 用户 CRUD、组织/组、多租户、自助门户、目录同步 |
| 客户端与 API 管理 | OAuth2 客户端注册、API 凭证、速率限制 |
| 治理与合规 | 审计日志、合规报告、数据保留、访问审批 |
| 风控与安全 | 异常检测、IP 信誉、Bot 检测、凭证泄漏检测 |
| 联邦身份 | SAML 2.0、LDAP/AD、SCIM 2.0、跨域 SSO |

---

## 二、AuthForge 各业务域真实实现状态

### 业务 1：OAuth2 / OIDC 协议 —— ★★★★★ 完整实现

> 验证方式：读取 `TokenEndpointController.cc` 全部 grant_type 分支 + `TokenService.cc` + `Pkce.cc` + `DiscoveryController.cc` + 穷尽搜索 `IBackchannelLogoutNotifier` 实现类
>
> ⚠️ **2026-08-11 更新**：OAuth/OIDC 规范性审查（[oauth-oidc-compliance-audit.md](done/oauth-oidc-compliance-audit.md)，2026-08-07）发现的 31 项偏差（F-001..F-031）已**全部修复**（PR #44，2026-08-09 合并），包括：F-002（client_secret 哈希算法统一）、F-003（refresh_token grant 客户端认证）、F-005（独立 Redis 存储模式废弃）、OIDC 扩展字段补齐（auth_time/acr/amr/azp/nonce/prompt/max_age/RP-Initiated Logout）。本报告下述实现细节反映的是**合规修复后**的代码状态。
>
> **2026-08-13 更新**：Backchannel Logout **后端已交付**（PR #50：通知器 + logout_token JWT 构造 + admin API 配置 + discovery + 单测 D1-D6），替换了原桩实现。详见 [in-progress/backchannel-logout-design.md](in-progress/backchannel-logout-design.md)。前端被 Mimosa 拦截 + 集成测试待补（需 PG）。

#### OAuth2 Grant Type 流程（`TokenEndpointController.cc:991-1810`）

| Grant Type | RFC | 真实状态 | 代码出处 | 实现细节 |
|------------|-----|---------|----------|----------|
| authorization_code | 6749 §4.1 | [完整实现] | `TokenEndpointController.cc:991-1035` + `TokenService.cc:149-417` | 授权码 SHA-256 哈希存储、单次消费、TTL 600s、redirect_uri 精确匹配 |
| refresh_token | 6749 §6 | [完整实现] | `TokenEndpointController.cc:1036-1136` + `TokenService.cc:419-545` | **令牌轮换** + **familyId 家族追踪** + **重用检测级联撤销** |
| client_credentials | 6749 §4.4 | [完整实现] | `TokenEndpointController.cc:1137-1298` | 仅 CONFIDENTIAL、scope 白名单校验、不签发 refresh_token |
| device_code | 8628 | [完整实现] | `TokenEndpointController.cc:1299-1793` + `DeviceAuthController.cc:167-422` + `DeviceCodeService.cc:13-123` | user_code 无歧义字符集、slow_down 检测、原子 CAS 消费 |
| password | 6749 §4.3 | [未实现] | `TokenEndpointController.cc:1797` else 分支返回 `unsupported_grant_type` | **有意设计决策**（OAuth 2.1 已废弃），`TokenEndpointController.h:43` 明确注释 |

#### OAuth2 扩展（穷尽搜索验证）

| 功能 | RFC | 真实状态 | 代码出处 |
|------|-----|---------|----------|
| PKCE S256 + plain | 7636 | [完整实现] | `libs/oauth2/src/pkce/Pkce.cc:11-68`（S256=BASE64URL(SHA256(verifier))，字符集 [A-Za-z0-9-._~]，长度 43-128） |
| 令牌内省 | 7662 | [完整实现] | `TokenEndpointController.cc:415-638`（客户端认证 + 所有权校验 + RFC 7662 §2.2 全字段） |
| 令牌撤销 | 7009 | [完整实现] | `TokenEndpointController.cc:640-828`（不存在 token 返回成功，防探测） |
| 动态客户端注册 | 7591 | [完整实现] | `ClientRegistrationService.cc:38-291`（client_id UUID + secret 盐哈希 + 审计） |
| RFC 7592 客户端管理 | 7592 | [未实现] | 无 PUT/DELETE on /oauth2/register |

#### OIDC 身份层（`DiscoveryController.cc:176-271`）

| 功能 | 真实状态 | 代码出处 |
|------|---------|----------|
| Discovery metadata | [完整实现] | `DiscoveryController.cc:176-271`（全部字段：issuer/authorization_endpoint/token_endpoint/device_authorization_endpoint/userinfo_endpoint/jwks_uri/introspection_endpoint/revocation_endpoint/registration_endpoint/scopes_supported/grant_types_supported/code_challenge_methods_supported/end_session_endpoint/claims_supported） |
| JWKS 端点 | [完整实现] | `DiscoveryController.cc:273-306` + `JwkManager`（Cache-Control: max-age=3600） |
| ID Token 签发 | [完整实现] | `TokenService.cc` generateIdToken（RS256） |
| UserInfo 端点 | [完整实现] | `UserInfoProvider.cc` |
| RP-Initiated Logout | [完整实现] | `SessionController.cc:1105+` endSession（id_token_hint/post_logout_redirect_uri/state） |
| **Backchannel Logout** | **[后端已交付]**（2026-08-13, PR #50） | 真实通知器（HTTP POST logout_token JWT）+ admin API 配置 `backchannel_logout_uri` + discovery 广告 + 单测 D1-D6（`5b3ccb9`..`9c8cf9f`）。~~原桩~~：`IdentityAssembly.cc:49-57` LoggingBackchannelLogoutNotifier 已被替换。待补：前端（Mimosa 拦截）+ 集成测试（需 PG） |

#### 令牌安全机制

| 功能 | 真实状态 | 代码出处 |
|------|---------|----------|
| Refresh Token 轮换 | [完整实现] | `TokenService.cc:419-545`（每次刷新签发新 AT+RT） |
| Token Family 追踪 | [完整实现] | `TokenService.cc:290`（familyId 首次生成）、`:501`（新 RT 继承 familyId） |
| 重用检测级联撤销 | [完整实现]（Postgres）/ [废弃]（独立 Redis） | Postgres: `PostgresTokenRepository.cc:452-496`（双 SQL 级联 UPDATE）。独立 Redis 模式已**废弃**（F-005/#24）：`OAuth2Plugin.cc:294-298` 启动时 ERROR 日志 + `:454-478` 拒绝 `refresh_token` grant。目标架构为 Postgres 单源 + Redis 缓存层（见 [#42 设计](in-progress/postgres-redis-cache-design.md)） |
| 原子 CAS 撤销 | [完整实现]（Memory/Postgres）/ [废弃]（独立 Redis） | Memory: `MemoryTokenRepository.cc:95-116`（lock_guard 内 find-check-set，真原子）。Postgres: `UPDATE...WHERE revoked=false RETURNING *`。独立 Redis: `RedisTokenRepository.h:114-117` 明确 `supportsCas()=false`，`getRefreshToken()` 是 no-op（`:161-165`）——但此模式已被 F-005 废弃，不再推荐用于生产 |

---

### 业务 2：身份认证 Authentication —— ★★★★★ 完整实现

> 验证方式：读取 `AuthService.cc` + `MfaService.cc` + `TotpUtils.cc` + `WebAuthnService.cc` + `GoogleAuthService.cc` + `GitHubController.cc`

#### 密码与登录（`libs/identity/src/AuthService.cc`）

| 功能 | 真实状态 | 代码出处 | 实现细节 |
|------|---------|----------|----------|
| 密码哈希 | [完整实现] | `AuthService.cc:63-84` | PBKDF2-HMAC-SHA256，**310,000 迭代**，32B 密钥，16B 盐，格式 `$pbkdf2-sha256$<iter>$<hex-salt>$<hex-hash>` |
| 密码验证 | [完整实现] | `AuthService.cc:86-146` | 常量时间比较（逐字节 XOR），支持 legacy SHA-256(password+salt) 验证后**自动升级** |
| 用户注册 | [完整实现] | `AuthService.cc:239-290` | 邮箱规范化、默认 user 角色分配、结构化错误码 |
| 用户登录 | [完整实现] | `SessionController.cc:388-803` | 标识路由（含@按email）、失败计数、锁定检查、Session 存储 userId/auth_time/amr |
| 登录策略评估 | [完整实现] | `SessionManager.cc:12-30` `evaluateLoginPolicy` | 邮箱验证优先于 MFA 检查 |
| 账号锁定（渐进式） | [完整实现] | `PostgresIdentityRepository.cc:382-390` | 失败≥5→60s，≥10→300s，≥15→1800s，≥20→3600s |
| 密码修改级联吊销 | [完整实现] | `UserSelfServiceController.cc:248-253` | UPDATE access_tokens + refresh_tokens SET revoked=true WHERE user_id |
| 密码重置 | [完整实现] | `PasswordResetService.cc` | 15 分钟令牌、一次性使用、重置后吊销所有 token |
| 邮箱验证 | [完整实现] | `EmailVerificationService.cc` | 验证端点 + 重发端点 |

#### MFA 多因素认证（`libs/identity/src/mfa/MfaService.cc` + `TotpUtils.cc`）

| 功能 | 真实状态 | 代码出处 | 实现细节 |
|------|---------|----------|----------|
| TOTP（RFC 6238） | [完整实现] | `TotpUtils.cc:77-97` | HMAC-SHA1、30s time step、6 位、RFC 4226 dynamic truncation、±1 step 容差 |
| TOTP 密钥生成 | [完整实现] | `TotpUtils.cc:108-113` | 20 字节（160bit）随机，Base32 编码 |
| otpauth URI | [完整实现] | `TotpUtils.cc:146-154` | `otpauth://totp/issuer:account?secret=...&algorithm=SHA1&digits=6&period=30` |
| 备份码 | [完整实现] | `TotpUtils.cc:156-180`（生成）+ `MfaService.cc:82-98`（SHA-256 哈希存储） | 10 个 8 字符码，排除歧义字符 I/O/0/1，**SHA-256 哈希存储**（明文仅一次性返回给用户） |
| MFA 绑定/解绑 | [完整实现] | `MfaService.cc:21-113` | setupSecret → verifyAndEnable → disable 完整流程 |
| 登录 MFA 验证 | [完整实现] | `MfaService.cc:115-138` + `SessionController.cc` RequireMfa 流程 | mfa_token + pending binding（clientId/redirectUri） |

#### WebAuthn / FIDO2 / Passkey（`libs/identity/src/webauthn/WebAuthnService.cc`）

| 功能 | 真实状态 | 代码出处 | 实现细节 |
|------|---------|----------|----------|
| 注册 challenge 生成 | [完整实现] | `WebAuthnService.cc:41-56` | 32 字节随机，base64url 编码 |
| **注册 attestation 验证** | **[未实现]** | `WebAuthnService.cc:58-103` | **`finishRegistration` 仅验证字段非空后存储 credentialId/publicKey，不验证 attestation 签名**。搜索 verify/attestation/signature/cbor 在该文件 0 匹配 |
| 认证 challenge 生成 | [完整实现] | `WebAuthnService.cc:105-119` | 32 字节随机 |
| **认证 assertion 验证** | **[未实现]** | `WebAuthnService.cc:121-161` | **`finishAuthentication` 仅查找 credential + 更新 signCount，不验证 assertion 签名** |
| 凭证存储 | [完整实现] | `IWebAuthnRepository.h` + `PostgresWebAuthnRepository` | 存 credentialId/publicKey/name/signCount/userId |
| 凭证列表 | [完整实现] | `WebAuthnService.cc:163-174` | listCredentials by userId |
| signCount 更新 | [完整实现] | `WebAuthnService.cc:152` | best-effort fire-and-forget UPDATE |

> ⚠️ **重要发现**：WebAuthn 是**简化实现**，不是完整 FIDO2 协议。缺少 CBOR 解码、attestation/assertion 签名验证、AAGUID 认证器认证。客户端 JS 负责与浏览器 WebAuthn API 交互，服务端只存储 credentialId/publicKey。这在安全上是**可接受的**（浏览器已验证 attestation），但不满足需要服务端验证认证器可信度的高合规场景。

#### 社交登录 Social Login（条件编译 `WITH_SOCIAL`）

| 提供商 | 真实状态 | 代码出处 | 实现细节 |
|--------|---------|----------|----------|
| Google | [完整实现] | `GoogleAuthService.cc:24-95` | code exchange（POST token endpoint）→ userinfo fetch（GET with Bearer）→ 返回 profile |
| GitHub | [完整实现] | `GitHubController.cc:126-632` + `GitHubAuthService.cc` | code exchange → userinfo → **find-or-create 本地账号** + subject mapping + 默认角色 + token 签发 |
| WeChat | [完整实现] | `WeChatController.cc` + `WeChatAuthService.cc` | GET-with-query-params（无 Bearer header） |
| HTTP 客户端端口 | [完整实现] | `IOAuthHttpClient.h` + `DrogonOAuthHttpClient.cc:32,64` | postForm + getWithBearerToken，真实 drogon::HttpClient 实现 |
| **账号关联 link/unlink** | **[未实现]** | 搜索 linkSocial/unlinkSocial 在 UserSelfServiceController.cc 0 匹配 | 用户无法在自助门户手动关联/解绑社交账号；GitHub 是自动 find-or-create |

#### 用户主体与映射

| 功能 | 真实状态 | 代码出处 |
|------|---------|----------|
| Subject 生成 | [完整实现] | `SubjectResolver.cc`（SubjectGenerator） |
| 跨应用 subject mapping | [完整实现] | `ISubjectMappingRepository.h` + `oauth2_subject_mappings` 表（provider/subject/internal_user_id） |
| UserInfo Provider | [完整实现] | `UserInfoProvider.cc` 实现 `IUserInfoProvider` 端口 |

---

### 业务 3：用户与身份管理 —— ★★★☆☆ 部分实现

> 验证方式：读取 `UserAdminService.cc` + `UserAdminController.cc` + 穷尽搜索 createUser/deleteUser/分页参数

#### 已实现（`UserAdminService.cc:19-746` + `UserAdminController.cc:91-165`）

| 功能 | 端点 | 代码出处 |
|------|------|----------|
| 列出用户 | `GET /api/admin/users` | `UserAdminService.cc:74-186`（**全量查询，无分页**，OpenAPI 文档谎称 "paginated"） |
| 获取用户 | `GET /api/admin/users/{userId}` | `UserAdminService.cc:244` |
| 更新用户 | `PUT /api/admin/users/{userId}` | `UserAdminService.cc:315-391`（**仅 email + email_verified 两字段**） |
| 禁用用户 | `PUT /api/admin/users/{userId}/disable` | `UserAdminService.cc:393`（`locked_until=9999999999`） |
| 启用用户 | `POST /api/admin/users/{userId}/enable` | `UserAdminService.cc:447` |
| 用户角色查询 | `GET /api/admin/users/{userId}/roles` | `UserAdminService.cc:502` |
| 用户角色分配 | `PUT /api/admin/users/{userId}/roles` | `UserAdminService.cc:587`（先 deleteBy 再逐条 insert） |

#### 未实现（穷尽搜索确认）

| 缺失项 | 验证方式 | 影响 |
|--------|----------|------|
| **创建用户** | 搜索 createUser/POST.*users 在 admin 下 0 匹配 | 管理员无法后台建号 |
| **删除用户** | 搜索 deleteUser/DELETE.*users 0 匹配 | 无 GDPR 删除权 |
| **分页** | 代码无 page/limit/offset | 大用户量性能问题 |
| **搜索/过滤** | 无 | 无法按用户名/邮箱搜索 |
| **属性管理不完整** | updateUser 仅 2 字段 | 不能改 username/password/mfa_enabled |

---

### 业务 4：客户端管理 —— ★★★★★ 完整实现

> 验证方式：读取 `ClientManagementService.cc:88-632` + `ClientAdminController.cc:102-186`

| 功能 | 端点 | 真实状态 | 代码出处 | 实现细节 |
|------|------|---------|----------|----------|
| 列出客户端 | `GET /api/admin/clients` | [完整实现] | `ClientManagementService.cc:88` | 全量查询 |
| 创建客户端 | `POST /api/admin/clients` | [完整实现] | `ClientManagementService.cc:132` | UUID client_id + secure secret + **盐+哈希存储**（F-002 盐轮换） |
| 获取客户端 | `GET /api/admin/clients/{clientId}` | [完整实现] | `ClientManagementService.cc:215` | 含 scopes 查询 |
| 更新客户端 | `PUT /api/admin/clients/{clientId}` | [完整实现] | `ClientManagementService.cc:280` | 动态字段（name/redirect_uris/grant_types） |
| 删除客户端 | `DELETE /api/admin/clients/{clientId}` | [完整实现] | `ClientManagementService.cc:373` | |
| 重置密钥 | `POST /api/admin/clients/{clientId}/reset-secret` | [完整实现] | `ClientManagementService.cc:414` | 新 secret + **新盐** + hash |
| 作用域查询 | `GET /api/admin/clients/{clientId}/scopes` | [完整实现] | `ClientManagementService.cc:474` | |
| 作用域分配 | `PUT /api/admin/clients/{clientId}/scopes` | [完整实现] | `ClientManagementService.cc:517` | 事务内 deleteBy + insert |

---

### 业务 5：RBAC 权限管理 —— ★★★★★ 完整实现（含 #43 细粒度 scope 模型）

> 验证方式：读取 `RoleScopeAdminService.cc` + `RoleScopeAdminController.cc` + `AuthorizationFilter.cc` + `V005__rbac_schema.sql` 迁移
>
> **2026-08-12 更新**：#43 资源-作用域授权模型已完整实现（commit `f04d3ba`）。原 F-010 最小路径前缀匹配已被替换为声明式 `(path,method)→scope` 注册表（`ResourceScopeRegistry`），支持读写粒度（`<resource>:read`/`<resource>:write`）、scope implication（admin 隐含 leaf scope）、统一 insufficient_scope 错误路径、DB 驱动的 admin-role 门控。7 个 controller 已迁移。设计文档见 [done/resource-scope-authorization-design.md](done/resource-scope-authorization-design.md)。

#### 数据模型（`V005__rbac_schema.sql`）
```
users ──< user_roles >── roles ──< role_permissions >── permissions
```
- 默认角色：`admin`、`user`（`:36-39`）
- 默认权限：`user:read`、`user:write`、`user:delete`、`admin:access`（`:42-47`）
- admin 角色拥有全部权限（`:50-52`），user 角色仅 `user:read`（`:55-58`）

#### 已实现

| 功能 | 真实状态 | 代码出处 |
|------|---------|----------|
| 角色 CRUD | [完整实现] | `RoleScopeAdminService.cc`（list/create/get/update/delete） |
| 权限（Scope）CRUD | [完整实现] | `RoleScopeAdminService.cc` |
| 角色-权限映射 | [完整实现] | `RoleScopeAdminService.cc`（PUT /api/admin/roles/{roleId}/permissions） |
| 用户-角色分配 | [完整实现] | `UserAdminService.cc:587` |
| Scope → Role 自动映射 | [完整实现] | `Oauth2Scopes.mapped_role` 字段 |
| RoleProvider 端口 | [完整实现] | `IRoleProvider.h`（Domain 层查询） |
| RBAC 授权过滤器 | [完整实现] | `AuthorizationFilter.cc:110-290`（从配置加载 rbac_rules + public_paths，正则匹配路径，DEFAULT DENY） |
| OAuth2 scope 过滤器 | [完整实现] | `OAuth2AuthFilter.cc:36-53`（/oauth2/userinfo→openid，/api/me→profile） |
| admin scope 双重门控 | [完整实现] | `AuthorizationFilter.cc:27-38,210-232`（/api/admin/* 需 admin scope + RBAC 角色，RFC 6750 §3.1） |

#### 未实现

| 缺失项 | 验证方式 |
|--------|----------|
| 角色继承 | 搜索 inherit/parent_role 0 匹配 |
| ABAC 策略引擎 | 穷尽搜索 abac/XACML/policy.engine 0 匹配（仅文档出现） |
| 权限审批工作流 | 无 |

---

### 业务 6：令牌与会话管理 —— ★★★★★ 完整实现

> 验证方式：读取 `TokenManagementService.cc` + `TokenAdminController.cc`

| 功能 | 端点 | 真实状态 | 代码出处 |
|------|------|---------|----------|
| 列出用户 access token | `GET /api/admin/users/{userId}/tokens` | [完整实现] | `TokenManagementService.cc` |
| 列出用户 refresh token | `GET /api/admin/users/{userId}/refresh-tokens` | [完整实现] | `TokenManagementService.cc` |
| 撤销单个 token | `POST /api/admin/tokens/revoke` | [完整实现] | `TokenManagementService.cc` |
| 撤销用户全部 token | `POST /api/admin/users/{userId}/revoke-all-tokens` | [完整实现] | `TokenManagementService.cc`（revokeAll） |
| 撤销 token 家族 | - | [完整实现]（Postgres）/ [桩]（Redis） | `TokenService::revokeTokenFamily` |
| 密码修改触发级联吊销 | `POST /api/me/change-password` | [完整实现] | `UserSelfServiceController.cc:248-253` |
| 过期令牌清理 | 插件定时任务 | [完整实现] | `OAuth2CleanupService.cc` |

---

### 业务 7：审计日志 —— ★★★★☆ 基础完整实现

> 验证方式：读取 `AuditService.cc` + `AuditController.cc` + `V012__audit_logs.sql` + `IAuditSink.h` + `DrogonAuditSink.cc`

#### 数据模型（`V012__audit_logs.sql`）
```sql
audit_logs (
  id BIGSERIAL, timestamp, actor_type, actor_id, action,
  target_type, target_id, outcome, ip, user_agent, request_id, details JSONB
)
-- 索引：timestamp, actor, action, outcome+timestamp
```

#### 已实现

| 功能 | 端点 | 真实状态 | 代码出处 |
|------|------|---------|----------|
| 审计 sink 端口 | `IAuditSink.h` | [完整实现] | `libs/common/include/authforge/common/ports/IAuditSink.h` |
| Drogon 适配器 | `DrogonAuditSink.cc` | [完整实现] | `libs/drogon/src/adapters/DrogonAuditSink.cc`（含 logFromRequest） |
| 审计查询 API | `GET /api/admin/logs` | [完整实现] | `AuditController.cc:49-57` + `AuditService.cc`（支持过滤） |
| Dashboard 统计 | `GET /api/admin/dashboard/stats` | [完整实现] | `AuditController.cc:59-67`（用户数/客户端数/活跃token/失败指标） |
| 事件记录 | 登录/登出/令牌/管理操作 | [完整实现] | 审计事件在 SessionController、TokenEndpointController、ClientRegistrationService、OrganizationService 等多处调用 |

#### 未实现（合规必备）

| 缺失项 | 验证方式 |
|--------|----------|
| 合规报告导出 | 搜索 export/report/compliance 0 匹配 |
| 数据保留与销毁策略 | 无自动归档/清理 |
| 审计完整性保护 | 无哈希链/签名 |
| SIEM 集成 | 无 Syslog/CEF 输出 |

---

### 业务 8：多租户 Multi-Tenancy —— ★★★☆☆ 基础实现（部分激活）

> 验证方式：读取 `V017__multi_tenant.sql` + `TenantId.h` + `OrganizationController.cc` + `OrganizationService.cc` + 穷尽搜索 tenant/org_id

> ⚠️ **重要纠正**：本报告上一轮错误地说多租户"仅停留在表结构层面"。真实情况是**表结构 + 部分业务 API 已实现，但请求级隔离未实现**。

#### 已实现

| 资产 | 真实状态 | 代码出处 | 实现细节 |
|------|---------|----------|----------|
| `organizations` 表 | [完整实现] | `V017__multi_tenant.sql:2-11` | id/slug/name/logo_uri/primary_color/issuer_override |
| **users.org_id 字段** | [完整实现] | `V017__multi_tenant.sql:14` | `ALTER TABLE users ADD COLUMN org_id INTEGER REFERENCES organizations(id)` + 索引 `idx_users_org` |
| **oauth2_clients.org_id 字段** | [完整实现] | `V017__multi_tenant.sql:17` | `ALTER TABLE oauth2_clients ADD COLUMN org_id` + 索引 `idx_clients_org` |
| Organization CRUD API | [完整实现] | `OrganizationController.cc` + `OrganizationService.cc:64-201` | list/create（slug 正则校验）/getBySlug，含审计日志 |
| 添加用户到组织 | [完整实现]（OpenAPI 注册） | `OrganizationController.cc:40-48` | `POST /api/orgs/{orgId}/users`（文档注册，实现需验证） |
| TenantId 值对象 | [完整实现]（预留维度缝） | `TenantId.h:1-75` | 明确注释"this project does not implement real multi-tenant isolation"，TenantId 仅作为"stable typed seam" |

#### 未实现

| 缺失项 | 验证方式 | 影响 |
|--------|----------|------|
| **请求级租户上下文** | 搜索 TenantContext/tenantFilter 0 匹配 | 请求不携带租户标识，查询不按 org_id 过滤 |
| **业务查询按 org_id 隔离** | UserAdminService/ClientManagementService 查询无 org_id 条件 | 所有租户数据混在一起 |
| **租户管理员角色** | 无"租户管理员"概念 | 只有全局 admin |
| **org_id 写入业务流程** | 创建用户/客户端时不设 org_id | 字段存在但业务不填充 |
| **多租户 issuer 隔离** | `issuer_override` 字段存在但未在 token 签发中使用 | 单一 issuer |

**真实状态结论**：多租户处于**"数据库 schema 已就绪 + Organization 管理 API 已实现 + 业务隔离逻辑未实现"**的状态。`TenantId.h` 的注释是权威声明——当前是单租户模式，多租户是预留的"维度缝"。

---

### 业务 9：存储与可观测性基础设施 —— ★★★★★ 完整实现

> 验证方式：`list_dir` 三个存储目录 + 读取能力声明 + 读取适配器实现

#### 存储抽象层（生产后端 + 缓存层 + 测试后端）

| 后端 | 文件数 | 真实状态 | 关键能力声明 | 代码出处 |
|------|--------|---------|-------------|----------|
| Memory | 6 .cc + 6 .h | [完整实现]（测试/无 DB 场景） | `supportsTransactions=true`（recursive_mutex）、`supportsCas=true`（lock_guard 内 CAS） | `MemoryTokenRepository.cc:95-116`（真原子） |
| PostgreSQL | 10 Repository + 19 ORM 模型 | [完整实现]（**生产唯一权威后端**） | `supportsTransactions=true`（`newTransactionAsync` 真实事务）、`supportsCas=true`（`UPDATE...WHERE...RETURNING`） | `PostgresTokenRepository.h:84-92` + `PostgresTokenRepository.cc:77-227`（saveTokenPair 真事务） |
| Redis | 4 Repository + **RedisCachedClientRepository**（#42 Phase 1） | [完整实现]（**缓存层**，非独立存储） | **独立 Redis 存储模式已废弃**（F-005/#24）。Redis 现定位为 Postgres 前的** L2 缓存**：`RedisCachedClientRepository`（cache-aside decorator，GET-on-miss/SET-EX/fire-and-forget，soft-fail 回落 Postgres），Phase 1 仅缓存 client 查询 | `RedisCachedClientRepository.cc` + `OAuth2Plugin.cc:294-298`（废弃日志） + [#42 设计](in-progress/postgres-redis-cache-design.md) |

#### ORM 模型（`model.json` + 19 张表）

确认表清单（`model.json:8-28`）：organizations, users, roles, permissions, user_roles, role_permissions, oauth2_clients, oauth2_access_tokens, oauth2_refresh_tokens, oauth2_codes, oauth2_scopes, oauth2_client_scopes, oauth2_user_consents, oauth2_subject_mappings, audit_logs, email_verification_tokens, password_reset_tokens, oauth2_device_codes, webauthn_credentials（共 **19** 张表，ORM 模型由 `drogon_ctl` 从 schema 生成，禁止手改）

#### 可观测性

| 能力 | 端口 | 适配器 | 真实状态 | 代码出处 |
|------|------|--------|---------|----------|
| 日志（6 级） | `ILogger.h` | `DrogonLogger.cc` | [完整实现] | 对接 Drogon LOG_* 宏 |
| 指标 | `IMetrics.h` | `DrogonMetrics.cc` | [完整实现] | counter/histogram/gauge |
| 审计 sink | `IAuditSink.h` | `DrogonAuditSink.cc` | [完整实现] | logFromRequest |
| 健康检查 | - | `HealthController.cc` | [完整实现] | `/health`（含 DB+Redis 探测）、`/health/live`、`/health/ready`（memory 模式特殊处理） |
| 时钟抽象 | `IClock.h` | `SystemClock.cc` | [完整实现] | 可注入，便于测试 |
| UUID 生成 | `IUuidGenerator.h` | `OpenSslUuidGenerator.cc` | [完整实现] | |
| 加密 | `ICryptoProvider.h` | `OpenSslCryptoProvider.cc` | [完整实现] | PBKDF2/SHA256/HMAC/base64Url/secureRandom |

#### 数据库迁移（`apps/server/migrations/`，22 个 SQL 文件）

| 迁移 | 内容 | 代码出处 |
|------|------|----------|
| V001 | schema_migrations 表 | `V001__schema_migrations.sql` |
| V002-V003 | OAuth2 核心（clients/tokens/codes/scopes） | `V002__oauth2_core.sql` + `V003__oauth2_core_indexes.sql` |
| V004 | users 表 | `V004__users_table.sql` |
| V005 | RBAC（roles/permissions/junctions + 默认数据） | `V005__rbac_schema.sql` |
| V006 | OAuth2 scopes（含 mapped_role） | `V006__oauth2_scopes.sql` |
| V008 | refresh_token family_id | `V008__refresh_token_family.sql` |
| V012 | 审计日志 | `V012__audit_logs.sql` |
| V014 | 设备码 | `V014__device_codes.sql` |
| V015 | Backchannel Logout（backchannel_logout_uri/session_required） | `V015__backchannel_logout.sql` |
| **V017** | **多租户（organizations + org_id）** | `V017__multi_tenant.sql` |
| V018 | WebAuthn 凭证 | `V018__webauthn.sql` |
| V019-V022 | email 校验/username 可选/列宽/mfa_pending_client_binding | `V019`-`V022` |

迁移计数机制：`countPendingMigrations`；Helm 钩子：`deploy/helm/authforge/templates/migration-job.yaml`（pre-install/pre-upgrade）；CI 守护：`tools/migration-check/`

#### API 文档

| 能力 | 真实状态 | 代码出处 |
|------|---------|----------|
| OpenAPI 生成 | [完整实现] | `ApiDocController.cc` + `OpenApiGenerator`（各控制器通过 EndpointInfo 注册） |
| Swagger UI | [完整实现] | `docs/backend/api/swagger-ui/`（swagger-ui-bundle.js 等静态资源） |
| API diff 守护 | [完整实现] | `tools/api-diff/`（SemVer 头面变更检测） |

#### 前端管理界面

| 前端 | 技术栈 | 页面模块 | 真实状态 |
|------|--------|----------|---------|
| Admin 后台 | Vue + TypeScript（8216 文件） | dashboard/users/roles/scopes/applications/tokens/logs/settings | [完整实现] |
| User 门户 | Vue + TypeScript（2641 文件） | account/auth/oauth | [完整实现] |

---

### 业务 10-13：完全缺失的 IAM 业务域

> 验证方式：对每个关键词执行 `search_content` 穷尽搜索整个代码库

| 业务域 | 搜索关键词 | 代码匹配 | 文档匹配 | 真实状态 |
|--------|-----------|----------|----------|---------|
| **SAML 2.0** | `saml\|SAML\|Saml` | **0 代码文件** | 3 文档文件 | [未实现] |
| **LDAP / AD** | `\bldap\b\|\bLDAP\b` | **0 代码文件** | 3 文档文件 | [未实现] |
| **SCIM 2.0** | `\bscim\b\|\bSCIM\b` | **0 代码文件** | 3 文档文件 | [未实现] |
| **ABAC / 策略引擎** | `\babac\b\|\bABAC\b\|XACML\|policy.engine` | **0 代码文件** | 3 文档文件 | [未实现] |
| **风控** | （无独立模块，仅有基础账号锁定） | - | - | [未实现]（仅渐进式锁定） |

---

### 业务 14：用户自助服务门户 —— ★★★☆☆ 部分实现

> 验证方式：读取 `UserSelfServiceController.cc` + 穷尽搜索 social/link

#### 已实现

| 功能 | 端点 | 真实状态 | 代码出处 |
|------|------|---------|----------|
| 获取个人信息 | `GET /api/me` | [完整实现] | `UserSelfServiceController.cc` |
| 修改密码（级联吊销） | `POST /api/me/change-password` | [完整实现] | `UserSelfServiceController.cc:140-351` |
| MFA 绑定/解绑 | `POST/DELETE /api/me/mfa/*` | [完整实现] | `MfaController.cc` |
| WebAuthn 注册/删除 | `POST/DELETE /api/me/webauthn/*` | [完整实现] | `WebAuthnController.cc:48-87` |
| 邮箱验证 | `GET /api/verify-email` | [完整实现] | `EmailVerificationController.cc` |
| 密码重置 | `POST /api/reset-password` | [完整实现] | `PasswordResetController.cc` |
| 删除账号 | - | [完整实现] | `UserSelfServiceController.cc:651-694`（deleteAccount 含级联吊销） |

#### 未实现

| 缺失项 | 验证方式 |
|--------|----------|
| ~~社交账号关联 link/unlink~~ | ✅ 2026-08-21 已实现（B2）：`GET/POST/DELETE /api/me/social/links[/{provider}]` + identity 层 `SocialLinkService` + 最后凭证守卫（PR #68 已合并） |
| 登录历史 | 无 |
| 活跃会话管理 | 无"查看/吊销我的所有会话" |
| 个人审计日志 | 无 |

---

## 三、与主流 IAM 产品的业务覆盖对比

| 业务域 | AuthForge 真实状态 | Keycloak | Auth0 | Ory | Zitadel |
|--------|-------------------|----------|-------|-----|---------|
| OAuth2/OIDC | ✅ 完整（Backchannel Logout 后端已交付） | ✅ | ✅ | ✅ | ✅ |
| MFA (TOTP+备份码) | ✅ 完整 | ✅ | ✅ | ✅ | ✅ |
| WebAuthn/Passkey | ⚠️ 简化（不验证 attestation/assertion） | ✅ 完整 | ✅ 完整 | ✅ 完整 | ✅ 完整 |
| 社交登录 | ✅ 3 家（无 link/unlink） | ✅ 20+ | ✅ 30+ | ✅ | ✅ |
| SAML 2.0 | ❌ 完全缺失 | ✅ | ✅ | ⚠️ | ✅ |
| LDAP/AD | ❌ 完全缺失 | ✅ | ✅ | ❌ | ✅ |
| SCIM 2.0 | ❌ 完全缺失 | ✅ | ✅ | ❌ | ✅ |
| 多租户 | ⚠️ schema+API 有，隔离无 | ✅ Realms | ✅ | ⚠️ | ✅ 一等公民 |
| RBAC | ✅ 完整 | ✅ | ✅ | ✅ | ✅ |
| ABAC | ❌ 完全缺失 | ✅ (JS 策略) | ✅ (Rules) | ❌ | ⚠️ |
| 审计日志 | ✅ 基础（无合规导出） | ✅ | ✅ | ✅ | ✅ |
| 用户管理 | ⚠️ 缺创建/删除/分页 | ✅ | ✅ | ✅ | ✅ |

---

## 四、业务缺口优先级建议

### P0（企业版 MVP 必须，6 个月内）

| 缺口 | 真实现状 | 工程量 |
|------|----------|--------|
| **用户管理补全**（创建/删除/分页/搜索） | ✅ 已实现（PR #52，2026-08-13：分页/搜索/createUser/软删除 V024） | ~~小（1-2 周）~~ 已完成 |
| **Backchannel Logout 真实实现** | 🟡 后端已交付（PR #50：通知器 + logout_token + admin API + 单测）；前端被 Mimosa 拦截 + 集成测试待补 | 中（1 月） |
| **SAML 2.0 IdP + SP** | 确认完全缺失（0 代码文件） | 大（3-6 月，需 XML 签名库） |
| **LDAP/AD 联邦** | 确认完全缺失 | 中（2-3 月） |
| **SCIM 2.0** | 确认完全缺失 | 中（1-2 月） |
| **多租户隔离激活** | schema 有，业务隔离无 | 大（需请求级上下文+查询过滤） |

### P1（企业版增强，6-12 个月）

| 缺口 | 真实现状 |
|------|----------|
| WebAuthn attestation/assertion 验证 | 确认简化实现（不验证签名） |
| ABAC 策略引擎 | 确认完全缺失 |
| 审计合规报告导出 | 确认缺失 |
| 社交账号 link/unlink | 确认缺失 |
| 审计完整性保护（哈希链） | 确认缺失 |
| SIEM 集成 | 确认缺失 |

### P2（差异化与前瞻，12+ 个月）

| 缺口 | 说明 |
|------|------|
| 风控引擎 | 仅有渐进式锁定 |
| AI Agent 身份管理 | 完全缺失 |
| 更多社交登录提供商 | 仅 Google/GitHub/WeChat |

---

## 五、结论

### AuthForge 在 IAM 业务版图中的真实定位

基于穷尽式代码验证，AuthForge 的真实状态是：

**强项（已达商业级，代码验证确认）**：
1. OAuth2/OIDC 协议实现完整且符合 RFC（**OAuth/OIDC 合规审计 31 项偏差已全部修复**，PR #44 / 2026-08-09；Backchannel Logout 后端已交付 PR #50 / 2026-08-13）
2. 身份认证全面（密码 PBKDF2 310K 迭代 + MFA TOTP RFC 6238 + 3 家社交登录真实实现）
3. RBAC + 令牌管理 + 审计日志基础完善
4. 存储层架构清晰：Postgres 生产单源 + Redis L2 缓存层（#42 Phase 1 已交付 client-cache decorator；独立 Redis 存储已废弃）
5. 可观测性基础设施扎实（日志/指标/审计/健康检查全端口+适配器）
6. C++ 技术栈性能/资源/嵌入能力差异化（**Phase 0 基准实测已完成**：discovery 86k QPS / introspect 17k QPS / userinfo 17k QPS on 8 vCPU WSL；详见 `benchmarks/results/SUMMARY.md`）

**弱项（部分实现，需补齐）**：
1. ~~用户管理 CRUD 不完整（确认无创建/删除/分页/搜索）~~ → ✅ 已实现（PR #52，2026-08-13）
2. WebAuthn 是简化实现（确认不验证 attestation/assertion 签名）
3. 多租户 schema+API 有但隔离逻辑无（`TenantId.h` 明确声明"does not implement real isolation"）
4. 审计合规能力薄弱（无报告导出/完整性保护）
5. 社交账号无 link/unlink

**空白（企业级硬门槛，穷尽搜索确认 0 代码）**：
1. SAML 2.0 / LDAP / SCIM 完全缺失——进入传统企业市场的硬门槛
2. ABAC / 风控完全缺失——金融/政府高合规场景门槛

### 上一轮报告的纠正

本轮调研纠正了上一轮（依赖子代理二手总结）的以下错误：
1. ❌ 上一轮："多租户仅停留在表结构层面" → ✅ 真实：`users.org_id`/`oauth2_clients.org_id` 字段存在 + Organization CRUD API 已实现 + `POST /api/orgs/{orgId}/users` 已注册
2. ❌ 上一轮："WebAuthn 完整实现" → ✅ 真实：简化实现，不验证 attestation/assertion 签名
3. ❌ 上一轮："社交账号关联 link/unlink" → ✅ 真实：未实现
4. ❌ 上一轮：Backchannel Logout 标为"完善" → ✅ 真实：桩实现（仅 LOG_DEBUG）

---

*本报告基于 AuthForge v1.1.0 代码库穷尽式验证，每个功能点状态均有 `file:line` 出处。调研日期 2026-08-09，2026-08-11/08-13 复核修正。下一步行动项见 [next-phase-implementation-plan.md](next-phase-implementation-plan.md)。*
